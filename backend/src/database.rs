use std::{
    collections::HashSet,
    fs,
    path::{Path, PathBuf},
    sync::Arc,
    time::Duration as StdDuration,
};

use anyhow::{Context, Result, anyhow};
use chrono::{DateTime, Duration, Utc};
use rusqlite::{Connection, OptionalExtension, Row, params};
use tokio::sync::Mutex;

use crate::domain::{
    ActivityLogEntry, AppLogLevel, AppSettings, AttachmentDiscovery, DownloadJobRecord,
    DownloadJobState, FailedJobRetentionUnit, MediaCategory, RoomCheckpoint, RoomHistoryMode,
    RoomRecord, SpaceAutoJoinRecord, TimeWindowUnit,
};

const MAX_RETAINED_LOG_ENTRIES: i64 = 5_000;
const LOG_RETENTION_DAYS: i64 = 30;

#[derive(Clone)]
pub struct AppDatabase {
    inner: Arc<Mutex<Connection>>,
    path: PathBuf,
}

impl AppDatabase {
    pub async fn open(path: impl AsRef<Path>) -> Result<Self> {
        let path = path.as_ref().to_owned();
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)
                .with_context(|| format!("Failed to create {}", parent.display()))?;
        }

        let connection = Connection::open(&path)
            .with_context(|| format!("Failed to open {}", path.display()))?;
        connection
            .busy_timeout(StdDuration::from_secs(5))
            .context("Failed to configure SQLite busy timeout")?;
        connection
            .pragma_update(None, "journal_mode", "WAL")
            .context("Failed to enable SQLite WAL mode")?;
        connection
            .pragma_update(None, "synchronous", "NORMAL")
            .context("Failed to configure SQLite synchronous mode")?;
        connection
            .pragma_update(None, "foreign_keys", "ON")
            .context("Failed to enable SQLite foreign keys")?;
        let database = Self {
            inner: Arc::new(Mutex::new(connection)),
            path,
        };
        database.initialize_schema().await?;
        database.harden_permissions()?;
        Ok(database)
    }

    pub fn path(&self) -> &Path {
        &self.path
    }

    pub async fn load_settings(&self, default_destination_root_path: &str) -> Result<AppSettings> {
        let connection = self.inner.lock().await;
        let mut statement =
            connection.prepare("SELECT * FROM app_settings ORDER BY id DESC LIMIT 1")?;
        let row = statement
            .query_row([], |row| Self::map_settings(row))
            .optional()
            .context("Failed to load app settings")?;

        if let Some(settings) = row {
            return Ok(settings);
        }

        let defaults = AppSettings {
            homeserver_url: "https://matrix.org".to_owned(),
            username: String::new(),
            owner_user_id: String::new(),
            destination_root_path: default_destination_root_path.to_owned(),
            message_limit: 5_000,
            time_window_value: 0,
            time_window_unit: TimeWindowUnit::None,
            retry_cooldown_minutes: 5,
            retry_limit: 10,
            download_worker_count: 1,
            failed_job_retention_value: 0,
            failed_job_retention_unit: FailedJobRetentionUnit::None,
            desired_power_state: false,
        };
        drop(statement);
        drop(connection);
        self.save_settings(&defaults).await?;
        Ok(defaults)
    }

    pub async fn save_settings(&self, settings: &AppSettings) -> Result<()> {
        let connection = self.inner.lock().await;
        connection.execute("DELETE FROM app_settings", [])?;
        connection.execute(
            "INSERT INTO app_settings (
                homeserver_url, username, owner_user_id, destination_root_path,
                message_limit, time_window_value, time_window_unit,
                retry_cooldown_minutes, retry_limit, download_worker_count,
                failed_job_retention_value, failed_job_retention_unit,
                desired_power_state, updated_at
            ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14)",
            params![
                settings.homeserver_url,
                settings.username,
                settings.owner_user_id,
                settings.destination_root_path,
                settings.message_limit,
                settings.time_window_value,
                time_window_unit_key(settings.time_window_unit),
                settings.retry_cooldown_minutes,
                settings.retry_limit,
                clamp_download_worker_count(settings.download_worker_count),
                settings.failed_job_retention_value,
                failed_retention_unit_key(settings.failed_job_retention_unit),
                if settings.desired_power_state { 1 } else { 0 },
                iso_now(),
            ],
        )?;
        drop(connection);
        self.harden_permissions()?;
        Ok(())
    }

    pub async fn upsert_room(
        &self,
        room_id: &str,
        display_name: Option<&str>,
        canonical_alias: Option<&str>,
        active_folder_label: &str,
        is_space: bool,
        membership: &str,
    ) -> Result<()> {
        let connection = self.inner.lock().await;
        connection.execute(
            "INSERT INTO rooms (room_id, display_name, canonical_alias, active_folder_label, is_space, membership, updated_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)
             ON CONFLICT(room_id) DO UPDATE SET
                display_name = excluded.display_name,
                canonical_alias = excluded.canonical_alias,
                active_folder_label = excluded.active_folder_label,
                is_space = excluded.is_space,
                membership = excluded.membership,
                updated_at = excluded.updated_at",
            params![
                room_id,
                display_name,
                canonical_alias,
                active_folder_label,
                if is_space { 1 } else { 0 },
                membership,
                iso_now(),
            ],
        )?;
        Ok(())
    }

    pub async fn fetch_rooms(&self) -> Result<Vec<RoomRecord>> {
        let connection = self.inner.lock().await;
        let mut statement = connection.prepare(
            "SELECT room_id, display_name, canonical_alias, active_folder_label, is_space, membership, updated_at
             FROM rooms
             ORDER BY COALESCE(display_name, canonical_alias, room_id) COLLATE NOCASE ASC",
        )?;
        let rows = statement.query_map([], Self::map_room_record)?;
        collect_rows(rows)
    }

    pub async fn room_record(&self, room_id: &str) -> Result<Option<RoomRecord>> {
        let connection = self.inner.lock().await;
        connection
            .query_row(
                "SELECT room_id, display_name, canonical_alias, active_folder_label, is_space, membership, updated_at
                 FROM rooms WHERE room_id = ?1",
                params![room_id],
                Self::map_room_record,
            )
            .optional()
            .context("Failed to load room record")
    }

    pub async fn all_folder_labels(
        &self,
        excluding_room_id: Option<&str>,
    ) -> Result<HashSet<String>> {
        let connection = self.inner.lock().await;
        let mut labels = HashSet::new();
        match excluding_room_id {
            Some(room_id) => {
                let mut statement = connection
                    .prepare("SELECT active_folder_label FROM rooms WHERE room_id <> ?1")?;
                let rows = statement.query_map(params![room_id], |row| row.get::<_, String>(0))?;
                for value in rows {
                    labels.insert(value?.to_lowercase());
                }
            }
            None => {
                let mut statement = connection.prepare("SELECT active_folder_label FROM rooms")?;
                let rows = statement.query_map([], |row| row.get::<_, String>(0))?;
                for value in rows {
                    labels.insert(value?.to_lowercase());
                }
            }
        }
        Ok(labels)
    }

    pub async fn insert_alias_history(&self, room_id: &str, aliases: &[String]) -> Result<()> {
        if aliases.is_empty() {
            return Ok(());
        }

        let connection = self.inner.lock().await;
        let now = iso_now();
        for alias in aliases {
            connection.execute(
                "INSERT INTO room_alias_history (room_id, alias, seen_at)
                 VALUES (?1, ?2, ?3)
                 ON CONFLICT(room_id, alias) DO UPDATE SET seen_at = excluded.seen_at",
                params![room_id, alias, now],
            )?;
        }
        Ok(())
    }

    pub async fn alias_history(&self, room_id: &str) -> Result<Vec<String>> {
        let connection = self.inner.lock().await;
        let mut statement = connection.prepare(
            "SELECT alias FROM room_alias_history WHERE room_id = ?1 ORDER BY seen_at DESC",
        )?;
        let rows = statement.query_map(params![room_id], |row| row.get::<_, String>(0))?;
        collect_rows(rows)
    }

    pub async fn upsert_space_auto_join_link(
        &self,
        space_room_id: &str,
        child_room_id: &str,
        auto_joined_by_bot: bool,
    ) -> Result<()> {
        let connection = self.inner.lock().await;
        let now = iso_now();
        connection.execute(
            "INSERT INTO space_auto_joins (
                space_room_id, child_room_id, auto_joined_by_bot, created_at, updated_at
             ) VALUES (?1, ?2, ?3, ?4, ?5)
             ON CONFLICT(space_room_id, child_room_id) DO UPDATE SET
                auto_joined_by_bot = CASE
                    WHEN space_auto_joins.auto_joined_by_bot = 1 OR excluded.auto_joined_by_bot = 1 THEN 1
                    ELSE 0
                END,
                updated_at = excluded.updated_at",
            params![
                space_room_id,
                child_room_id,
                if auto_joined_by_bot { 1 } else { 0 },
                now,
                now,
            ],
        )?;
        Ok(())
    }

    pub async fn fetch_space_auto_join_links_for_space(
        &self,
        space_room_id: &str,
    ) -> Result<Vec<SpaceAutoJoinRecord>> {
        let connection = self.inner.lock().await;
        let mut statement = connection.prepare(
            "SELECT * FROM space_auto_joins WHERE space_room_id = ?1 ORDER BY child_room_id COLLATE NOCASE ASC",
        )?;
        let rows = statement.query_map(params![space_room_id], Self::map_space_auto_join)?;
        collect_rows(rows)
    }

    pub async fn fetch_space_auto_join_links_for_child(
        &self,
        child_room_id: &str,
    ) -> Result<Vec<SpaceAutoJoinRecord>> {
        let connection = self.inner.lock().await;
        let mut statement = connection.prepare(
            "SELECT * FROM space_auto_joins WHERE child_room_id = ?1 ORDER BY space_room_id COLLATE NOCASE ASC",
        )?;
        let rows = statement.query_map(params![child_room_id], Self::map_space_auto_join)?;
        collect_rows(rows)
    }

    pub async fn delete_space_auto_join_link(
        &self,
        space_room_id: &str,
        child_room_id: &str,
    ) -> Result<()> {
        let connection = self.inner.lock().await;
        connection.execute(
            "DELETE FROM space_auto_joins WHERE space_room_id = ?1 AND child_room_id = ?2",
            params![space_room_id, child_room_id],
        )?;
        Ok(())
    }

    pub async fn load_checkpoint(&self, room_id: &str) -> Result<RoomCheckpoint> {
        let connection = self.inner.lock().await;
        let checkpoint = connection
            .query_row(
                "SELECT * FROM room_scan_state WHERE room_id = ?1",
                params![room_id],
                Self::map_checkpoint,
            )
            .optional()?;

        if let Some(checkpoint) = checkpoint {
            return Ok(checkpoint);
        }

        connection.execute(
            "INSERT INTO room_scan_state (
                room_id, historical_message_count, initial_backfill_complete, last_history_mode
             ) VALUES (?1, 0, 0, ?2)",
            params![room_id, RoomHistoryMode::Idle.as_storage_key()],
        )?;

        Ok(RoomCheckpoint {
            room_id: room_id.to_owned(),
            last_processed_event_id: None,
            last_processed_timestamp: None,
            oldest_backfilled_event_id: None,
            oldest_backfilled_timestamp: None,
            historical_message_count: 0,
            initial_backfill_complete: false,
            last_history_mode: RoomHistoryMode::Idle,
            last_history_run_at: None,
        })
    }

    pub async fn save_checkpoint(&self, checkpoint: &RoomCheckpoint) -> Result<()> {
        let connection = self.inner.lock().await;
        connection.execute(
            "INSERT INTO room_scan_state (
                room_id, last_processed_event_id, last_processed_ts,
                oldest_backfilled_event_id, oldest_backfilled_ts,
                historical_message_count, initial_backfill_complete,
                last_history_mode, last_history_run_at
             ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)
             ON CONFLICT(room_id) DO UPDATE SET
                last_processed_event_id = excluded.last_processed_event_id,
                last_processed_ts = excluded.last_processed_ts,
                oldest_backfilled_event_id = excluded.oldest_backfilled_event_id,
                oldest_backfilled_ts = excluded.oldest_backfilled_ts,
                historical_message_count = excluded.historical_message_count,
                initial_backfill_complete = excluded.initial_backfill_complete,
                last_history_mode = excluded.last_history_mode,
                last_history_run_at = excluded.last_history_run_at",
            params![
                checkpoint.room_id,
                checkpoint.last_processed_event_id,
                checkpoint.last_processed_timestamp.as_ref().map(iso_string),
                checkpoint.oldest_backfilled_event_id,
                checkpoint
                    .oldest_backfilled_timestamp
                    .as_ref()
                    .map(iso_string),
                checkpoint.historical_message_count,
                if checkpoint.initial_backfill_complete {
                    1
                } else {
                    0
                },
                checkpoint.last_history_mode.as_storage_key(),
                checkpoint.last_history_run_at.as_ref().map(iso_string),
            ],
        )?;
        Ok(())
    }

    pub async fn reset_all_history_scans_for_full_rescan(&self) -> Result<()> {
        let connection = self.inner.lock().await;
        connection.execute(
            "UPDATE room_scan_state
             SET
                last_processed_event_id = NULL,
                last_processed_ts = NULL,
                oldest_backfilled_event_id = NULL,
                oldest_backfilled_ts = NULL,
                historical_message_count = 0,
                initial_backfill_complete = 0,
                last_history_mode = ?1,
                last_history_run_at = NULL",
            params![RoomHistoryMode::Idle.as_storage_key()],
        )?;
        connection.execute("DELETE FROM discovered_attachments", [])?;
        connection.execute("DELETE FROM download_jobs", [])?;
        Ok(())
    }

    pub async fn enqueue_discovery(&self, discovery: &AttachmentDiscovery) -> Result<bool> {
        let connection = self.inner.lock().await;
        connection.execute(
            "INSERT OR IGNORE INTO discovered_attachments (
                room_id, event_id, origin_ts, mxc_url, original_filename, mime_type, category
             ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
            params![
                discovery.room_id,
                discovery.event_id,
                iso_string(&discovery.origin_server_timestamp),
                discovery.mxc_url,
                discovery.original_filename,
                discovery.mime_type,
                discovery.category.as_storage_key(),
            ],
        )?;
        if connection.changes() == 0 {
            return Ok(false);
        }

        let now = iso_now();
        connection.execute(
            "INSERT OR IGNORE INTO download_jobs (
                room_id, event_id, mxc_url, original_filename, mime_type, category,
                state, retry_count, created_at, updated_at
             ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, 0, ?8, ?9)",
            params![
                discovery.room_id,
                discovery.event_id,
                discovery.mxc_url,
                discovery.original_filename,
                discovery.mime_type,
                discovery.category.as_storage_key(),
                DownloadJobState::Queued.as_storage_key(),
                now,
                now,
            ],
        )?;
        Ok(true)
    }

    pub async fn mark_job_queued(&self, id: i64, last_error: Option<&str>) -> Result<()> {
        let connection = self.inner.lock().await;
        connection.execute(
            "UPDATE download_jobs
             SET state = ?1, last_error = ?2, next_eligible_at = NULL, updated_at = ?3
             WHERE id = ?4",
            params![
                DownloadJobState::Queued.as_storage_key(),
                last_error,
                iso_now(),
                id
            ],
        )?;
        Ok(())
    }

    pub async fn retry_permanent_failed_job(&self, id: i64) -> Result<bool> {
        let connection = self.inner.lock().await;
        connection.execute(
            "UPDATE download_jobs
             SET state = ?1, retry_count = 0, next_eligible_at = NULL, last_failure_at = NULL, last_error = NULL, updated_at = ?2
             WHERE id = ?3 AND state = ?4",
            params![
                DownloadJobState::Queued.as_storage_key(),
                iso_now(),
                id,
                DownloadJobState::FailedPermanent.as_storage_key(),
            ],
        )?;
        Ok(connection.changes() > 0)
    }

    pub async fn retry_all_permanent_failed_jobs(&self) -> Result<usize> {
        let connection = self.inner.lock().await;
        connection.execute(
            "UPDATE download_jobs
             SET state = ?1, retry_count = 0, next_eligible_at = NULL, last_failure_at = NULL, last_error = NULL, updated_at = ?2
             WHERE state = ?3",
            params![
                DownloadJobState::Queued.as_storage_key(),
                iso_now(),
                DownloadJobState::FailedPermanent.as_storage_key(),
            ],
        )?;
        Ok(connection.changes() as usize)
    }

    pub async fn clear_permanent_failed_job(&self, id: i64) -> Result<bool> {
        let connection = self.inner.lock().await;
        connection.execute(
            "DELETE FROM download_jobs WHERE id = ?1 AND state = ?2",
            params![id, DownloadJobState::FailedPermanent.as_storage_key()],
        )?;
        Ok(connection.changes() > 0)
    }

    pub async fn clear_all_permanent_failed_jobs(&self) -> Result<usize> {
        let connection = self.inner.lock().await;
        connection.execute(
            "DELETE FROM download_jobs WHERE state = ?1",
            params![DownloadJobState::FailedPermanent.as_storage_key()],
        )?;
        Ok(connection.changes() as usize)
    }

    pub async fn prune_permanent_failed_jobs(&self, older_than: DateTime<Utc>) -> Result<usize> {
        let connection = self.inner.lock().await;
        connection.execute(
            "DELETE FROM download_jobs
             WHERE state = ?1
               AND COALESCE(last_failure_at, updated_at, created_at) <= ?2",
            params![
                DownloadJobState::FailedPermanent.as_storage_key(),
                iso_string(&older_than)
            ],
        )?;
        Ok(connection.changes() as usize)
    }

    pub async fn reset_interrupted_jobs(&self) -> Result<()> {
        let connection = self.inner.lock().await;
        connection.execute(
            "UPDATE download_jobs
             SET state = ?1, updated_at = ?2, last_error = COALESCE(last_error, 'Interrupted download was reset on launch')
             WHERE state = ?3",
            params![
                DownloadJobState::Queued.as_storage_key(),
                iso_now(),
                DownloadJobState::Downloading.as_storage_key(),
            ],
        )?;
        Ok(())
    }

    pub async fn claim_next_eligible_job(
        &self,
        now: DateTime<Utc>,
    ) -> Result<Option<DownloadJobRecord>> {
        let connection = self.inner.lock().await;
        let mut statement = connection.prepare(
            "SELECT * FROM download_jobs
             WHERE
                state = ?1
                OR (state = ?2 AND (next_eligible_at IS NULL OR next_eligible_at <= ?4))
                OR (state = ?3 AND (next_eligible_at IS NULL OR next_eligible_at <= ?4))
             ORDER BY COALESCE(last_failure_at, created_at) ASC, id ASC
             LIMIT 1",
        )?;
        let job = statement
            .query_row(
                params![
                    DownloadJobState::Queued.as_storage_key(),
                    DownloadJobState::CoolingDown.as_storage_key(),
                    DownloadJobState::UndecryptablePending.as_storage_key(),
                    iso_string(&now),
                ],
                Self::map_job,
            )
            .optional()?;
        drop(statement);

        let Some(mut job) = job else {
            return Ok(None);
        };

        let updated_at = Utc::now();
        connection.execute(
            "UPDATE download_jobs
             SET state = ?1, last_error = NULL, next_eligible_at = NULL, updated_at = ?2
             WHERE id = ?3",
            params![
                DownloadJobState::Downloading.as_storage_key(),
                iso_string(&updated_at),
                job.id
            ],
        )?;

        job.state = DownloadJobState::Downloading;
        job.last_error = None;
        job.next_eligible_at = None;
        job.updated_at = updated_at;
        Ok(Some(job))
    }

    pub async fn mark_job_completed(
        &self,
        id: i64,
        sha256: &str,
        saved_relative_path: &str,
    ) -> Result<()> {
        let connection = self.inner.lock().await;
        connection.execute(
            "UPDATE download_jobs
             SET state = ?1, sha256 = ?2, saved_relative_path = ?3, updated_at = ?4
             WHERE id = ?5",
            params![
                DownloadJobState::Completed.as_storage_key(),
                sha256,
                saved_relative_path,
                iso_now(),
                id,
            ],
        )?;
        Ok(())
    }

    pub async fn mark_job_duplicate(
        &self,
        id: i64,
        sha256: &str,
        saved_relative_path: Option<&str>,
    ) -> Result<()> {
        let connection = self.inner.lock().await;
        connection.execute(
            "UPDATE download_jobs
             SET state = ?1, sha256 = ?2, saved_relative_path = ?3, updated_at = ?4
             WHERE id = ?5",
            params![
                DownloadJobState::DuplicateCompleted.as_storage_key(),
                sha256,
                saved_relative_path,
                iso_now(),
                id,
            ],
        )?;
        Ok(())
    }

    pub async fn mark_job_cooling_down(
        &self,
        id: i64,
        retry_count: i32,
        next_eligible_at: DateTime<Utc>,
        error: &str,
        permanently_failed: bool,
    ) -> Result<()> {
        let connection = self.inner.lock().await;
        connection.execute(
            "UPDATE download_jobs
             SET state = ?1, retry_count = ?2, next_eligible_at = ?3, last_failure_at = ?4, last_error = ?5, updated_at = ?6
             WHERE id = ?7",
            params![
                if permanently_failed {
                    DownloadJobState::FailedPermanent.as_storage_key()
                } else {
                    DownloadJobState::CoolingDown.as_storage_key()
                },
                retry_count,
                iso_string(&next_eligible_at),
                iso_now(),
                error,
                iso_now(),
                id,
            ],
        )?;
        Ok(())
    }

    pub async fn mark_job_undecryptable(
        &self,
        id: i64,
        next_eligible_at: DateTime<Utc>,
        error: &str,
    ) -> Result<()> {
        let connection = self.inner.lock().await;
        let failed_at = Utc::now();
        connection.execute(
            "UPDATE download_jobs
             SET state = ?1, next_eligible_at = ?2, last_failure_at = ?3, last_error = ?4, updated_at = ?5
             WHERE id = ?6",
            params![
                DownloadJobState::UndecryptablePending.as_storage_key(),
                iso_string(&next_eligible_at),
                iso_string(&failed_at),
                error,
                iso_string(&failed_at),
                id,
            ],
        )?;
        Ok(())
    }

    pub async fn find_completed_job(
        &self,
        room_id: &str,
        category: MediaCategory,
        sha256: &str,
    ) -> Result<Option<DownloadJobRecord>> {
        let connection = self.inner.lock().await;
        connection
            .query_row(
                "SELECT * FROM download_jobs
                 WHERE room_id = ?1 AND category = ?2 AND sha256 = ?3 AND state IN (?4, ?5)
                 LIMIT 1",
                params![
                    room_id,
                    category.as_storage_key(),
                    sha256,
                    DownloadJobState::Completed.as_storage_key(),
                    DownloadJobState::DuplicateCompleted.as_storage_key(),
                ],
                Self::map_job,
            )
            .optional()
            .context("Failed to query completed job")
    }

    pub async fn discovery_origin_timestamp(
        &self,
        room_id: &str,
        event_id: &str,
    ) -> Result<Option<DateTime<Utc>>> {
        let connection = self.inner.lock().await;
        let value: Option<String> = connection
            .query_row(
                "SELECT origin_ts FROM discovered_attachments WHERE room_id = ?1 AND event_id = ?2 LIMIT 1",
                params![room_id, event_id],
                |row| row.get(0),
            )
            .optional()?
            .flatten();
        parse_optional_datetime(value)
    }

    pub async fn fetch_jobs(
        &self,
        limit: i64,
        now: DateTime<Utc>,
    ) -> Result<Vec<DownloadJobRecord>> {
        let connection = self.inner.lock().await;
        let mut statement = connection.prepare(
            "SELECT * FROM download_jobs
             WHERE state IN (?1, ?2, ?3, ?4)
             ORDER BY
                CASE state
                    WHEN ?5 THEN 0
                    WHEN ?6 THEN
                        CASE
                            WHEN next_eligible_at IS NULL OR next_eligible_at <= ?8 THEN 0
                            ELSE 1
                        END
                    WHEN ?7 THEN
                        CASE
                            WHEN next_eligible_at IS NULL OR next_eligible_at <= ?8 THEN 0
                            ELSE 1
                        END
                    WHEN ?9 THEN 2
                    ELSE 3
                END,
                COALESCE(last_failure_at, created_at) ASC,
                id ASC
             LIMIT ?10",
        )?;
        let rows = statement.query_map(
            params![
                DownloadJobState::Queued.as_storage_key(),
                DownloadJobState::CoolingDown.as_storage_key(),
                DownloadJobState::UndecryptablePending.as_storage_key(),
                DownloadJobState::FailedPermanent.as_storage_key(),
                DownloadJobState::Queued.as_storage_key(),
                DownloadJobState::CoolingDown.as_storage_key(),
                DownloadJobState::UndecryptablePending.as_storage_key(),
                iso_string(&now),
                DownloadJobState::FailedPermanent.as_storage_key(),
                limit,
            ],
            Self::map_job,
        )?;
        collect_rows(rows)
    }

    pub async fn fetch_waiting_job_count(&self) -> Result<i64> {
        let connection = self.inner.lock().await;
        connection
            .query_row(
                "SELECT COUNT(*) FROM download_jobs WHERE state IN (?1, ?2, ?3)",
                params![
                    DownloadJobState::Queued.as_storage_key(),
                    DownloadJobState::CoolingDown.as_storage_key(),
                    DownloadJobState::UndecryptablePending.as_storage_key(),
                ],
                |row| row.get(0),
            )
            .context("Failed to count waiting jobs")
    }

    pub async fn fetch_recent_logs(&self, limit: i64) -> Result<Vec<ActivityLogEntry>> {
        let connection = self.inner.lock().await;
        let mut statement =
            connection.prepare("SELECT * FROM activity_log ORDER BY id DESC LIMIT ?1")?;
        let rows = statement.query_map(params![limit], Self::map_log_entry)?;
        let mut collected = collect_rows(rows)?;
        collected.reverse();
        Ok(collected)
    }

    pub async fn insert_log(
        &self,
        level: AppLogLevel,
        subsystem: &str,
        message: &str,
    ) -> Result<()> {
        let connection = self.inner.lock().await;
        let now = Utc::now();
        connection.execute(
            "INSERT INTO activity_log (created_at, level, subsystem, message) VALUES (?1, ?2, ?3, ?4)",
            params![iso_string(&now), level.as_storage_key(), subsystem, message],
        )?;

        let cutoff = now - Duration::days(LOG_RETENTION_DAYS);
        connection.execute(
            "DELETE FROM activity_log WHERE created_at < ?1",
            params![iso_string(&cutoff)],
        )?;
        connection.execute(
            "DELETE FROM activity_log
             WHERE id IN (
                SELECT id FROM activity_log ORDER BY id DESC LIMIT -1 OFFSET ?1
             )",
            params![MAX_RETAINED_LOG_ENTRIES],
        )?;
        Ok(())
    }

    async fn initialize_schema(&self) -> Result<()> {
        let connection = self.inner.lock().await;
        connection.execute_batch(
            "CREATE TABLE IF NOT EXISTS app_settings (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                homeserver_url TEXT NOT NULL,
                username TEXT NOT NULL,
                owner_user_id TEXT NOT NULL,
                destination_root_path TEXT NOT NULL,
                message_limit INTEGER NOT NULL,
                time_window_value INTEGER NOT NULL,
                time_window_unit TEXT NOT NULL,
                retry_cooldown_minutes INTEGER NOT NULL,
                retry_limit INTEGER NOT NULL,
                download_worker_count INTEGER NOT NULL DEFAULT 1,
                failed_job_retention_value INTEGER NOT NULL DEFAULT 0,
                failed_job_retention_unit TEXT NOT NULL DEFAULT 'none',
                desired_power_state INTEGER NOT NULL,
                updated_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS rooms (
                room_id TEXT PRIMARY KEY,
                display_name TEXT,
                canonical_alias TEXT,
                active_folder_label TEXT NOT NULL,
                is_space INTEGER NOT NULL DEFAULT 0,
                membership TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS room_alias_history (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                room_id TEXT NOT NULL,
                alias TEXT NOT NULL,
                seen_at TEXT NOT NULL,
                UNIQUE(room_id, alias)
            );
            CREATE TABLE IF NOT EXISTS room_scan_state (
                room_id TEXT PRIMARY KEY,
                last_processed_event_id TEXT,
                last_processed_ts TEXT,
                oldest_backfilled_event_id TEXT,
                oldest_backfilled_ts TEXT,
                historical_message_count INTEGER NOT NULL DEFAULT 0,
                initial_backfill_complete INTEGER NOT NULL DEFAULT 0,
                last_history_mode TEXT NOT NULL DEFAULT 'idle',
                last_history_run_at TEXT
            );
            CREATE TABLE IF NOT EXISTS discovered_attachments (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                room_id TEXT NOT NULL,
                event_id TEXT NOT NULL,
                origin_ts TEXT NOT NULL,
                mxc_url TEXT NOT NULL,
                original_filename TEXT,
                mime_type TEXT,
                category TEXT NOT NULL,
                UNIQUE(room_id, event_id)
            );
            CREATE TABLE IF NOT EXISTS download_jobs (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                room_id TEXT NOT NULL,
                event_id TEXT NOT NULL,
                mxc_url TEXT NOT NULL,
                original_filename TEXT,
                mime_type TEXT,
                category TEXT NOT NULL,
                state TEXT NOT NULL,
                retry_count INTEGER NOT NULL DEFAULT 0,
                next_eligible_at TEXT,
                last_failure_at TEXT,
                last_error TEXT,
                sha256 TEXT,
                saved_relative_path TEXT,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL,
                UNIQUE(room_id, event_id)
            );
            CREATE TABLE IF NOT EXISTS activity_log (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                created_at TEXT NOT NULL,
                level TEXT NOT NULL,
                subsystem TEXT NOT NULL,
                message TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS space_auto_joins (
                space_room_id TEXT NOT NULL,
                child_room_id TEXT NOT NULL,
                auto_joined_by_bot INTEGER NOT NULL DEFAULT 0,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL,
                PRIMARY KEY(space_room_id, child_room_id)
            );",
        )?;
        Ok(())
    }

    fn harden_permissions(&self) -> Result<()> {
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;

            for path in [
                self.path.clone(),
                self.path.with_extension("sqlite-wal"),
                self.path.with_extension("sqlite-shm"),
                PathBuf::from(format!("{}-wal", self.path.display())),
                PathBuf::from(format!("{}-shm", self.path.display())),
            ] {
                if path.exists() {
                    fs::set_permissions(&path, fs::Permissions::from_mode(0o600))
                        .with_context(|| format!("Failed to secure {}", path.display()))?;
                }
            }
        }
        Ok(())
    }

    fn map_settings(row: &Row<'_>) -> rusqlite::Result<AppSettings> {
        Ok(AppSettings {
            homeserver_url: row.get("homeserver_url")?,
            username: row.get("username")?,
            owner_user_id: row.get("owner_user_id")?,
            destination_root_path: row.get("destination_root_path")?,
            message_limit: row.get("message_limit")?,
            time_window_value: row.get("time_window_value")?,
            time_window_unit: parse_time_window_unit(
                row.get::<_, String>("time_window_unit")?.as_str(),
            ),
            retry_cooldown_minutes: row.get("retry_cooldown_minutes")?,
            retry_limit: row.get("retry_limit")?,
            download_worker_count: clamp_download_worker_count(row.get("download_worker_count")?),
            failed_job_retention_value: row.get("failed_job_retention_value")?,
            failed_job_retention_unit: parse_failed_retention_unit(
                row.get::<_, String>("failed_job_retention_unit")?.as_str(),
            ),
            desired_power_state: row.get::<_, i64>("desired_power_state")? != 0,
        })
    }

    fn map_room_record(row: &Row<'_>) -> rusqlite::Result<RoomRecord> {
        Ok(RoomRecord {
            room_id: row.get("room_id")?,
            current_display_name: row.get("display_name")?,
            current_canonical_alias: row.get("canonical_alias")?,
            active_folder_label: row.get("active_folder_label")?,
            is_space: row.get::<_, i64>("is_space")? != 0,
            membership: row.get("membership")?,
            updated_at: parse_datetime_required(row.get("updated_at")?)?,
        })
    }

    fn map_checkpoint(row: &Row<'_>) -> rusqlite::Result<RoomCheckpoint> {
        Ok(RoomCheckpoint {
            room_id: row.get("room_id")?,
            last_processed_event_id: row.get("last_processed_event_id")?,
            last_processed_timestamp: parse_datetime_optional_row(row, "last_processed_ts")?,
            oldest_backfilled_event_id: row.get("oldest_backfilled_event_id")?,
            oldest_backfilled_timestamp: parse_datetime_optional_row(row, "oldest_backfilled_ts")?,
            historical_message_count: row.get("historical_message_count")?,
            initial_backfill_complete: row.get::<_, i64>("initial_backfill_complete")? != 0,
            last_history_mode: RoomHistoryMode::from_storage_key(
                &row.get::<_, String>("last_history_mode")?,
            ),
            last_history_run_at: parse_datetime_optional_row(row, "last_history_run_at")?,
        })
    }

    fn map_job(row: &Row<'_>) -> rusqlite::Result<DownloadJobRecord> {
        Ok(DownloadJobRecord {
            id: row.get("id")?,
            room_id: row.get("room_id")?,
            event_id: row.get("event_id")?,
            mxc_url: row.get("mxc_url")?,
            original_filename: row.get("original_filename")?,
            mime_type: row.get("mime_type")?,
            category: MediaCategory::from_storage_key(&row.get::<_, String>("category")?),
            state: DownloadJobState::from_storage_key(&row.get::<_, String>("state")?),
            retry_count: row.get("retry_count")?,
            next_eligible_at: parse_datetime_optional_row(row, "next_eligible_at")?,
            last_failure_at: parse_datetime_optional_row(row, "last_failure_at")?,
            last_error: row.get("last_error")?,
            sha256: row.get("sha256")?,
            saved_relative_path: row.get("saved_relative_path")?,
            created_at: parse_datetime_required(row.get("created_at")?)?,
            updated_at: parse_datetime_required(row.get("updated_at")?)?,
        })
    }

    fn map_log_entry(row: &Row<'_>) -> rusqlite::Result<ActivityLogEntry> {
        Ok(ActivityLogEntry {
            id: row.get("id")?,
            created_at: parse_datetime_required(row.get("created_at")?)?,
            level: AppLogLevel::from_storage_key(&row.get::<_, String>("level")?),
            subsystem: row.get("subsystem")?,
            message: row.get("message")?,
        })
    }

    fn map_space_auto_join(row: &Row<'_>) -> rusqlite::Result<SpaceAutoJoinRecord> {
        Ok(SpaceAutoJoinRecord {
            space_room_id: row.get("space_room_id")?,
            child_room_id: row.get("child_room_id")?,
            auto_joined_by_bot: row.get::<_, i64>("auto_joined_by_bot")? != 0,
            created_at: parse_datetime_required(row.get("created_at")?)?,
            updated_at: parse_datetime_required(row.get("updated_at")?)?,
        })
    }
}

fn collect_rows<T>(
    rows: rusqlite::MappedRows<'_, impl FnMut(&Row<'_>) -> rusqlite::Result<T>>,
) -> Result<Vec<T>> {
    let mut collected = Vec::new();
    for row in rows {
        collected.push(row?);
    }
    Ok(collected)
}

fn parse_datetime_required(value: String) -> rusqlite::Result<DateTime<Utc>> {
    DateTime::parse_from_rfc3339(&value)
        .map(|value| value.with_timezone(&Utc))
        .map_err(|error| {
            rusqlite::Error::FromSqlConversionFailure(
                0,
                rusqlite::types::Type::Text,
                Box::new(error),
            )
        })
}

fn parse_datetime_optional_row(
    row: &Row<'_>,
    column: &str,
) -> rusqlite::Result<Option<DateTime<Utc>>> {
    let value: Option<String> = row.get(column)?;
    match value {
        Some(value) => Ok(Some(parse_datetime_required(value)?)),
        None => Ok(None),
    }
}

fn parse_optional_datetime(value: Option<String>) -> Result<Option<DateTime<Utc>>> {
    match value {
        Some(value) => Ok(Some(
            DateTime::parse_from_rfc3339(&value)
                .with_context(|| format!("Invalid timestamp: {value}"))?
                .with_timezone(&Utc),
        )),
        None => Ok(None),
    }
}

fn iso_now() -> String {
    iso_string(&Utc::now())
}

fn iso_string(value: &DateTime<Utc>) -> String {
    value.to_rfc3339_opts(chrono::SecondsFormat::Millis, true)
}

fn time_window_unit_key(unit: TimeWindowUnit) -> &'static str {
    match unit {
        TimeWindowUnit::None => "none",
        TimeWindowUnit::Day => "day",
        TimeWindowUnit::Week => "week",
        TimeWindowUnit::Month => "month",
    }
}

fn parse_time_window_unit(value: &str) -> TimeWindowUnit {
    match value {
        "day" => TimeWindowUnit::Day,
        "week" => TimeWindowUnit::Week,
        "month" => TimeWindowUnit::Month,
        _ => TimeWindowUnit::None,
    }
}

fn failed_retention_unit_key(unit: FailedJobRetentionUnit) -> &'static str {
    match unit {
        FailedJobRetentionUnit::None => "none",
        FailedJobRetentionUnit::Minute => "minute",
        FailedJobRetentionUnit::Hour => "hour",
        FailedJobRetentionUnit::Day => "day",
    }
}

fn parse_failed_retention_unit(value: &str) -> FailedJobRetentionUnit {
    match value {
        "minute" => FailedJobRetentionUnit::Minute,
        "hour" => FailedJobRetentionUnit::Hour,
        "day" => FailedJobRetentionUnit::Day,
        _ => FailedJobRetentionUnit::None,
    }
}

fn clamp_download_worker_count(value: i32) -> i32 {
    value.clamp(1, 6)
}

pub fn failed_job_cutoff_date(settings: &AppSettings) -> Option<DateTime<Utc>> {
    let value = settings.failed_job_retention_value;
    if value <= 0 {
        return None;
    }

    match settings.failed_job_retention_unit {
        FailedJobRetentionUnit::None => None,
        FailedJobRetentionUnit::Minute => Some(Utc::now() - Duration::minutes(value as i64)),
        FailedJobRetentionUnit::Hour => Some(Utc::now() - Duration::hours(value as i64)),
        FailedJobRetentionUnit::Day => Some(Utc::now() - Duration::days(value as i64)),
    }
}

pub fn should_stop_initial_backfill(
    checkpoint: &RoomCheckpoint,
    settings: &AppSettings,
) -> Result<bool> {
    if settings.message_limit > 0 && checkpoint.historical_message_count >= settings.message_limit {
        return Ok(true);
    }

    if settings.time_window_value <= 0 || matches!(settings.time_window_unit, TimeWindowUnit::None)
    {
        return Ok(false);
    }

    let Some(oldest_timestamp) = checkpoint.oldest_backfilled_timestamp else {
        return Ok(false);
    };

    let cutoff = match settings.time_window_unit {
        TimeWindowUnit::None => return Ok(false),
        TimeWindowUnit::Day => Utc::now() - Duration::days(settings.time_window_value as i64),
        TimeWindowUnit::Week => Utc::now() - Duration::weeks(settings.time_window_value as i64),
        TimeWindowUnit::Month => {
            Utc::now() - Duration::days((settings.time_window_value as i64) * 30)
        }
    };

    Ok(oldest_timestamp <= cutoff)
}

pub fn ensure_relative_to_root(root: &str, path: &str) -> Result<String> {
    path.strip_prefix(&(root.to_owned() + "/"))
        .map(ToOwned::to_owned)
        .ok_or_else(|| anyhow!("Path {path} is not inside root {root}"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use uuid::Uuid;

    fn temp_database_path() -> PathBuf {
        std::env::temp_dir().join(format!(
            "matrix-media-archiver-db-test-{}.sqlite3",
            Uuid::new_v4()
        ))
    }

    fn sample_discovery(event_id: &str) -> AttachmentDiscovery {
        AttachmentDiscovery {
            room_id: "!room:example.org".to_owned(),
            event_id: event_id.to_owned(),
            origin_server_timestamp: Utc::now(),
            mxc_url: format!("mxc://example.org/{event_id}"),
            original_filename: Some(format!("{event_id}.bin")),
            mime_type: Some("application/octet-stream".to_owned()),
            category: MediaCategory::Other,
        }
    }

    async fn set_job_created_at(
        database: &AppDatabase,
        event_id: &str,
        created_at: DateTime<Utc>,
    ) -> Result<()> {
        let connection = database.inner.lock().await;
        connection.execute(
            "UPDATE download_jobs SET created_at = ?1, updated_at = ?1 WHERE event_id = ?2",
            params![iso_string(&created_at), event_id],
        )?;
        Ok(())
    }

    async fn remove_database_files(path: &Path) {
        let _ = tokio::fs::remove_file(path).await;
        let _ = tokio::fs::remove_file(path.with_extension("sqlite3-shm")).await;
        let _ = tokio::fs::remove_file(path.with_extension("sqlite3-wal")).await;
    }

    #[tokio::test]
    async fn claim_next_eligible_job_skips_ineligible_undecryptable_entries() {
        let path = temp_database_path();
        let database = AppDatabase::open(&path).await.expect("open test database");
        let now = Utc::now();

        database
            .enqueue_discovery(&sample_discovery("$blocked"))
            .await
            .expect("enqueue blocked job");
        database
            .enqueue_discovery(&sample_discovery("$ready"))
            .await
            .expect("enqueue ready job");

        set_job_created_at(&database, "$blocked", now - Duration::minutes(10))
            .await
            .expect("age blocked job");
        set_job_created_at(&database, "$ready", now - Duration::minutes(5))
            .await
            .expect("age ready job");

        let jobs = database.fetch_jobs(10, now).await.expect("fetch jobs");
        let blocked_job = jobs
            .iter()
            .find(|job| job.event_id == "$blocked")
            .expect("blocked job should exist");
        database
            .mark_job_undecryptable(blocked_job.id, now + Duration::minutes(5), "missing keys")
            .await
            .expect("mark blocked job undecryptable");

        let claimed = database
            .claim_next_eligible_job(now)
            .await
            .expect("claim job")
            .expect("expected an eligible job");

        assert_eq!(claimed.event_id, "$ready");

        drop(database);
        remove_database_files(&path).await;
    }

    #[tokio::test]
    async fn fetch_jobs_keeps_ineligible_retries_behind_ready_queue_items() {
        let path = temp_database_path();
        let database = AppDatabase::open(&path).await.expect("open test database");
        let now = Utc::now();

        database
            .enqueue_discovery(&sample_discovery("$first"))
            .await
            .expect("enqueue first job");
        database
            .enqueue_discovery(&sample_discovery("$blocked"))
            .await
            .expect("enqueue blocked job");
        database
            .enqueue_discovery(&sample_discovery("$third"))
            .await
            .expect("enqueue third job");

        set_job_created_at(&database, "$first", now - Duration::minutes(12))
            .await
            .expect("age first job");
        set_job_created_at(&database, "$blocked", now - Duration::minutes(11))
            .await
            .expect("age blocked job");
        set_job_created_at(&database, "$third", now - Duration::minutes(10))
            .await
            .expect("age third job");

        let jobs = database.fetch_jobs(10, now).await.expect("fetch jobs");
        let blocked_job = jobs
            .iter()
            .find(|job| job.event_id == "$blocked")
            .expect("blocked job should exist");
        database
            .mark_job_undecryptable(blocked_job.id, now + Duration::minutes(5), "missing keys")
            .await
            .expect("mark blocked job undecryptable");

        let ordered_jobs = database
            .fetch_jobs(10, now)
            .await
            .expect("fetch ordered jobs");
        let ordered_event_ids: Vec<_> = ordered_jobs
            .iter()
            .map(|job| job.event_id.as_str())
            .collect();

        assert_eq!(ordered_event_ids, vec!["$first", "$third", "$blocked"]);
        assert_eq!(
            ordered_jobs[2].state,
            DownloadJobState::UndecryptablePending
        );
        assert!(ordered_jobs[2].next_eligible_at.is_some());
        assert!(ordered_jobs[2].last_failure_at.is_some());

        drop(database);
        remove_database_files(&path).await;
    }
}
