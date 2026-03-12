use std::{
    collections::{HashMap, HashSet},
    path::{Path, PathBuf},
    process::Stdio,
    sync::Arc,
    time::Duration,
};

use anyhow::{Context, Result, anyhow};
use chrono::{DateTime, Utc};
use eyeball_im::{Vector, VectorDiff};
use filetime::{FileTime, set_file_mtime};
use futures_util::StreamExt;
use matrix_sdk::{
    Client, Room,
    encryption::{
        VerificationState,
        verification::{SasVerification, VerificationRequest},
    },
    media::{MediaFormat, MediaRequestParameters},
    ruma::{
        OwnedMxcUri, OwnedServerName, RoomAliasId, RoomOrAliasId, ServerName, UserId,
        events::room::{
            MediaSource,
            message::{MessageType, RoomMessageEventContent},
        },
    },
};
use matrix_sdk_ui::{
    sync_service::{State as SyncState, SyncService},
    timeline::{RoomExt, TimelineItem},
};
use mime::Mime;
use reqwest::header::{ACCEPT, AUTHORIZATION, USER_AGENT};
use serde::Serialize;
use sha2::{Digest, Sha256};
use tokio::{
    fs::{self as tokio_fs, File as TokioFile},
    io::AsyncWriteExt,
    sync::{Mutex, RwLock, mpsc},
    task::JoinHandle,
    time::sleep,
};
use uuid::Uuid;

use crate::{
    app_paths::AppPaths,
    database::AppDatabase,
    domain::{
        ActiveDownloadSnapshot, AppLogLevel, AppSettings, AttachmentDiscovery, BotRuntimeSnapshot,
        ConnectionState, DownloadJobRecord, FailedJobRetentionUnit, MediaCategory, RoomCheckpoint,
        RoomHierarchySnapshot, RoomHistoryMode, RoomWorkerSnapshot, SpaceChildDescriptor,
        StoredSession, VerificationEmoji, VerificationSnapshot, VerificationStatus,
    },
    media_classification,
    protocol::{Command, ServerEvent},
    room_catalog::RoomCatalog,
    secret_store::SecretStore,
};

const ROOM_REFRESH_INTERVAL: Duration = Duration::from_secs(15);
const DOWNLOAD_IDLE_DELAY: Duration = Duration::from_secs(1);
const DOWNLOAD_ERROR_DELAY: Duration = Duration::from_secs(2);

pub enum CommandOutcome {
    Continue,
    Shutdown,
}

pub struct BackendService {
    paths: AppPaths,
    database: AppDatabase,
    secret_store: SecretStore,
    runtime: RuntimeStore,
    event_tx: mpsc::UnboundedSender<ServerEvent>,
    running: Option<RunningService>,
}

struct RunningService {
    context: Arc<RunningContext>,
    handles: Vec<JoinHandle<()>>,
}

struct RunningContext {
    paths: AppPaths,
    database: AppDatabase,
    secret_store: SecretStore,
    room_catalog: RoomCatalog,
    runtime: RuntimeStore,
    client: Client,
    sync_service: Arc<SyncService>,
    settings: Arc<RwLock<AppSettings>>,
    downloads: Arc<DownloadManager>,
    room_workers: Arc<Mutex<HashMap<String, RoomWorkerState>>>,
    handled_event_ids: Arc<Mutex<HashSet<String>>>,
    verification: Arc<Mutex<VerificationContext>>,
}

struct RoomWorkerState {
    live_task: Option<JoinHandle<()>>,
    history_task: Option<JoinHandle<()>>,
    live_watcher_active: bool,
    history_mode: RoomHistoryMode,
    history_detail: String,
}

impl RoomWorkerState {
    fn new() -> Self {
        Self {
            live_task: None,
            history_task: None,
            live_watcher_active: false,
            history_mode: RoomHistoryMode::Idle,
            history_detail: "Idle".to_owned(),
        }
    }

    fn snapshot(&self, room_id: &str) -> RoomWorkerSnapshot {
        RoomWorkerSnapshot {
            room_id: room_id.to_owned(),
            live_watcher_active: self.live_watcher_active,
            history_mode: self.history_mode,
            history_detail: self.history_detail.clone(),
        }
    }
}

#[derive(Default)]
struct VerificationContext {
    request: Option<VerificationRequest>,
    sas: Option<SasVerification>,
    sas_task: Option<JoinHandle<()>>,
}

#[derive(Clone)]
struct RuntimeStore {
    state: Arc<Mutex<BotRuntimeSnapshot>>,
    event_tx: mpsc::UnboundedSender<ServerEvent>,
}

impl RuntimeStore {
    fn new(event_tx: mpsc::UnboundedSender<ServerEvent>) -> Self {
        Self {
            state: Arc::new(Mutex::new(BotRuntimeSnapshot::default())),
            event_tx,
        }
    }

    async fn snapshot(&self) -> BotRuntimeSnapshot {
        self.state.lock().await.clone()
    }

    async fn replace(&self, next: BotRuntimeSnapshot) {
        *self.state.lock().await = next.clone();
        let _ = self.event_tx.send(ServerEvent::Runtime { snapshot: next });
    }

    async fn mutate<F>(&self, callback: F)
    where
        F: FnOnce(&mut BotRuntimeSnapshot),
    {
        let snapshot = {
            let mut state = self.state.lock().await;
            callback(&mut state);
            state.clone()
        };
        let _ = self.event_tx.send(ServerEvent::Runtime { snapshot });
    }
}

#[derive(Clone)]
struct DownloadManager {
    database: AppDatabase,
    room_catalog: RoomCatalog,
    paths: AppPaths,
    runtime: RuntimeStore,
    settings: Arc<RwLock<AppSettings>>,
    client: Client,
    workers: Arc<Mutex<HashMap<i32, JoinHandle<()>>>>,
}

#[derive(Clone, Serialize)]
struct EncodedMediaSource<'a> {
    #[serde(rename = "kind")]
    source_kind: &'a str,
    #[serde(skip_serializing_if = "Option::is_none")]
    plain: Option<&'a OwnedMxcUri>,
    #[serde(skip_serializing_if = "Option::is_none")]
    encrypted: Option<&'a matrix_sdk::ruma::events::room::EncryptedFile>,
}

impl BackendService {
    pub async fn new(
        paths: AppPaths,
        event_tx: mpsc::UnboundedSender<ServerEvent>,
    ) -> Result<Self> {
        paths.ensure_directories()?;
        let database = AppDatabase::open(&paths.database_path).await?;
        let secret_store = SecretStore::new(paths.secret_store_path.clone());
        let runtime = RuntimeStore::new(event_tx.clone());

        Ok(Self {
            paths,
            database,
            secret_store,
            runtime,
            event_tx,
            running: None,
        })
    }

    pub async fn handle_command(&mut self, command: Command) -> Result<CommandOutcome> {
        match command {
            Command::Start { settings, password } => {
                self.start(settings, password).await?;
                Ok(CommandOutcome::Continue)
            }
            Command::Stop => {
                self.stop().await?;
                Ok(CommandOutcome::Continue)
            }
            Command::SaveSettings { settings, password } => {
                self.save_settings(settings, password).await?;
                Ok(CommandOutcome::Continue)
            }
            Command::JoinRoom { room_id_or_alias } => {
                let running = self.running_context()?;
                join_room(&running, &room_id_or_alias, &[]).await?;
                refresh_joined_rooms(running).await?;
                Ok(CommandOutcome::Continue)
            }
            Command::LeaveRoom { room_id } => {
                let running = self.running_context()?;
                leave_room(&running, &room_id).await?;
                refresh_joined_rooms(running).await?;
                Ok(CommandOutcome::Continue)
            }
            Command::RequestVerification => {
                let running = self.running_context()?;
                request_verification(&running).await?;
                Ok(CommandOutcome::Continue)
            }
            Command::StartSasVerification => {
                let running = self.running_context()?;
                start_sas_verification(&running).await?;
                Ok(CommandOutcome::Continue)
            }
            Command::ApproveVerification => {
                let running = self.running_context()?;
                approve_verification(&running).await?;
                Ok(CommandOutcome::Continue)
            }
            Command::DeclineVerification => {
                let running = self.running_context()?;
                decline_verification(&running).await?;
                Ok(CommandOutcome::Continue)
            }
            Command::ResetHistoryScans => {
                self.reset_history_scans().await?;
                Ok(CommandOutcome::Continue)
            }
            Command::Shutdown => {
                self.stop().await?;
                Ok(CommandOutcome::Shutdown)
            }
        }
    }

    fn running_context(&self) -> Result<Arc<RunningContext>> {
        self.running
            .as_ref()
            .map(|running| running.context.clone())
            .ok_or_else(|| anyhow!("Matrix client is not connected."))
    }

    async fn start(&mut self, settings: AppSettings, password: String) -> Result<()> {
        if self.running.is_some() {
            self.stop().await?;
        }

        self.paths.ensure_directories()?;
        self.database.save_settings(&settings).await?;
        self.secret_store.save_password(&password)?;
        cleanup_temp_files(&self.paths.temp_downloads_path).await?;
        self.database.reset_interrupted_jobs().await?;

        self.runtime
            .replace(BotRuntimeSnapshot {
                connection_state: ConnectionState::Starting,
                ..BotRuntimeSnapshot::default()
            })
            .await;

        let client = connect_client(&self.paths, &self.secret_store, &settings, &password).await?;
        persist_current_session(&self.secret_store, &client).await?;

        let current_user_id = runtime_user_id_from_settings(&settings)
            .ok_or_else(|| anyhow!("Connected Matrix client has no configured user id"))?;
        let device_id = client
            .device_id()
            .map(ToString::to_string)
            .ok_or_else(|| anyhow!("Connected Matrix client has no device id"))?;
        let account_mode = if current_user_id == settings.owner_user_id {
            "sharedOwnerAccount"
        } else {
            "dedicatedBot"
        }
        .to_owned();

        let sync_service = Arc::new(SyncService::builder(client.clone()).build().await?);
        let settings_store = Arc::new(RwLock::new(settings.clone()));
        let room_catalog = RoomCatalog::new(self.database.clone());
        let downloads = Arc::new(DownloadManager::new(
            self.database.clone(),
            room_catalog.clone(),
            self.paths.clone(),
            self.runtime.clone(),
            settings_store.clone(),
            client.clone(),
        ));
        let context = Arc::new(RunningContext {
            paths: self.paths.clone(),
            database: self.database.clone(),
            secret_store: self.secret_store.clone(),
            room_catalog,
            runtime: self.runtime.clone(),
            client: client.clone(),
            sync_service: sync_service.clone(),
            settings: settings_store,
            downloads: downloads.clone(),
            room_workers: Arc::new(Mutex::new(HashMap::new())),
            handled_event_ids: Arc::new(Mutex::new(HashSet::new())),
            verification: Arc::new(Mutex::new(VerificationContext::default())),
        });

        self.runtime
            .mutate(|runtime| {
                runtime.connection_state = ConnectionState::Starting;
                runtime.current_user_id = Some(current_user_id.clone());
                runtime.device_id = Some(device_id.clone());
                runtime.account_mode = Some(account_mode.clone());
            })
            .await;
        refresh_verification_snapshot(&context).await?;

        downloads.start().await;
        sync_service.start().await;
        publish_sync_state(&context, &sync_service.state().get()).await;
        refresh_joined_rooms(context.clone()).await?;

        let handles = vec![
            tokio::spawn(watch_sync_state(context.clone())),
            tokio::spawn(watch_session_changes(context.clone())),
            tokio::spawn(watch_verification_state(context.clone())),
            tokio::spawn(periodic_room_refresh(context.clone())),
        ];
        self.running = Some(RunningService { context, handles });

        Ok(())
    }

    async fn stop(&mut self) -> Result<()> {
        let Some(running) = self.running.take() else {
            self.runtime
                .replace(BotRuntimeSnapshot {
                    connection_state: ConnectionState::Stopped,
                    ..BotRuntimeSnapshot::default()
                })
                .await;
            return Ok(());
        };

        let context = running.context;
        for handle in running.handles {
            handle.abort();
        }

        {
            let mut verification = context.verification.lock().await;
            if let Some(task) = verification.sas_task.take() {
                task.abort();
            }
            verification.request = None;
            verification.sas = None;
        }

        stop_all_room_workers(&context).await;
        context.downloads.stop().await;
        context.sync_service.stop().await;

        self.runtime
            .replace(BotRuntimeSnapshot {
                connection_state: ConnectionState::Stopped,
                ..BotRuntimeSnapshot::default()
            })
            .await;
        Ok(())
    }

    async fn save_settings(&mut self, settings: AppSettings, password: String) -> Result<()> {
        let previous_settings = self
            .running
            .as_ref()
            .map(|running| running.context.settings.clone());
        let previous_settings = match previous_settings {
            Some(settings_store) => settings_store.read().await.clone(),
            None => self.database.load_settings("").await?,
        };
        let previous_password = self.secret_store.load_password().unwrap_or_default();

        self.database.save_settings(&settings).await?;
        self.secret_store.save_password(&password)?;

        if let Some(running) = &self.running {
            *running.context.settings.write().await = settings.clone();
        }

        let requires_restart = previous_settings.homeserver_url != settings.homeserver_url
            || previous_settings.username != settings.username
            || previous_settings.owner_user_id != settings.owner_user_id
            || previous_password != password;

        if requires_restart && settings.desired_power_state {
            self.start(settings, password).await?;
            return Ok(());
        }

        if !settings.desired_power_state {
            self.stop().await?;
            return Ok(());
        }

        if let Some(running) = &self.running {
            running.context.downloads.restart().await;
            refresh_joined_rooms(running.context.clone()).await?;
        }

        Ok(())
    }

    async fn reset_history_scans(&mut self) -> Result<()> {
        self.database
            .reset_all_history_scans_for_full_rescan()
            .await?;
        if let Some(running) = &self.running {
            stop_all_room_workers(&running.context).await;
            running.context.downloads.restart().await;
            refresh_joined_rooms(running.context.clone()).await?;
        }
        Ok(())
    }
}

impl DownloadManager {
    fn new(
        database: AppDatabase,
        room_catalog: RoomCatalog,
        paths: AppPaths,
        runtime: RuntimeStore,
        settings: Arc<RwLock<AppSettings>>,
        client: Client,
    ) -> Self {
        Self {
            database,
            room_catalog,
            paths,
            runtime,
            settings,
            client,
            workers: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    async fn start(&self) {
        self.stop().await;

        let worker_count = self.settings.read().await.download_worker_count.clamp(1, 6);
        let mut workers = self.workers.lock().await;
        for worker_id in 1..=worker_count {
            let manager = self.clone();
            workers.insert(
                worker_id,
                tokio::spawn(async move {
                    manager.worker_loop(worker_id).await;
                }),
            );
        }
    }

    async fn stop(&self) {
        let mut workers = self.workers.lock().await;
        let handles = workers
            .drain()
            .map(|(_, handle)| handle)
            .collect::<Vec<_>>();
        drop(workers);

        for handle in handles {
            handle.abort();
        }

        self.runtime
            .mutate(|runtime| {
                runtime.active_downloads.clear();
            })
            .await;
    }

    async fn restart(&self) {
        self.start().await;
    }

    async fn worker_loop(self, worker_id: i32) {
        loop {
            let connection_state = self.runtime.snapshot().await.connection_state;
            if connection_state != ConnectionState::Running {
                sleep(DOWNLOAD_IDLE_DELAY).await;
                continue;
            }

            match self.database.claim_next_eligible_job(Utc::now()).await {
                Ok(Some(job)) => {
                    if let Err(error) = self.process_job(worker_id, job).await {
                        let _ = self
                            .database
                            .insert_log(
                                AppLogLevel::Error,
                                "queue",
                                &format!("Queue worker {worker_id} failed: {error:#}"),
                            )
                            .await;
                        sleep(DOWNLOAD_ERROR_DELAY).await;
                    }
                }
                Ok(None) => sleep(DOWNLOAD_IDLE_DELAY).await,
                Err(error) => {
                    let _ = self
                        .database
                        .insert_log(
                            AppLogLevel::Error,
                            "queue",
                            &format!("Queue polling failed: {error:#}"),
                        )
                        .await;
                    sleep(DOWNLOAD_ERROR_DELAY).await;
                }
            }
        }
    }

    async fn process_job(&self, worker_id: i32, job: DownloadJobRecord) -> Result<()> {
        let file_name = job
            .original_filename
            .clone()
            .unwrap_or_else(|| job.event_id.clone());
        self.set_active_download(
            worker_id,
            Some(ActiveDownloadSnapshot {
                worker_id,
                job_id: job.id,
                room_id: job.room_id.clone(),
                filename: file_name.clone(),
                received_bytes: 0,
                total_bytes: None,
            }),
        )
        .await;

        let result = self.process_job_inner(worker_id, &job).await;
        self.set_active_download(worker_id, None).await;
        result
    }

    async fn process_job_inner(&self, worker_id: i32, job: &DownloadJobRecord) -> Result<()> {
        let settings = self.settings.read().await.clone();
        let temp_path = download_media_to_temp(
            &self.client,
            &self.paths,
            &self.runtime,
            worker_id,
            job,
            settings.homeserver_url.clone(),
        )
        .await;

        let temp_path = match temp_path {
            Ok(temp_path) => temp_path,
            Err(error) => {
                handle_job_failure(&self.database, &self.settings, job, &error).await?;
                return Ok(());
            }
        };

        let result = async {
            validate_downloaded_media(&temp_path, job.category).await?;
            let sha256 = sha256_file(&temp_path).await?;

            if let Some(existing) = self
                .database
                .find_completed_job(&job.room_id, job.category, &sha256)
                .await?
            {
                self.database
                    .mark_job_duplicate(job.id, &sha256, existing.saved_relative_path.as_deref())
                    .await?;
                self.database
                    .insert_log(
                        AppLogLevel::Info,
                        "queue",
                        &format!(
                            "Skipped duplicate {}",
                            job.original_filename
                                .clone()
                                .unwrap_or_else(|| job.event_id.clone())
                        ),
                    )
                    .await?;
                return Ok(());
            }

            let category_folder = self
                .room_catalog
                .category_folder(&job.room_id, job.category, &settings)
                .await?;
            let ext = media_classification::preferred_extension(
                job.original_filename.as_deref(),
                job.mime_type.as_deref(),
            );
            let final_name = ext.map_or_else(|| sha256.clone(), |ext| format!("{sha256}.{ext}"));
            let final_path = category_folder.join(final_name);
            let relative_path = relative_storage_path(&settings.destination_root_path, &final_path);

            if final_path.exists() {
                self.database
                    .mark_job_duplicate(job.id, &sha256, Some(&relative_path))
                    .await?;
                return Ok(());
            }

            move_file_cross_filesystem(&temp_path, &final_path).await?;
            if let Some(origin) = self
                .database
                .discovery_origin_timestamp(&job.room_id, &job.event_id)
                .await?
            {
                let _ = set_file_mtime(
                    &final_path,
                    FileTime::from_unix_time(origin.timestamp(), origin.timestamp_subsec_nanos()),
                );
            }

            self.database
                .mark_job_completed(job.id, &sha256, &relative_path)
                .await?;
            self.database
                .insert_log(
                    AppLogLevel::Info,
                    "queue",
                    &format!(
                        "Downloaded {} -> {}",
                        job.original_filename
                            .clone()
                            .unwrap_or_else(|| job.event_id.clone()),
                        relative_path
                    ),
                )
                .await?;
            Ok(())
        }
        .await;

        if result.is_err() {
            let _ = tokio_fs::remove_file(&temp_path).await;
        }
        result
    }

    async fn set_active_download(&self, worker_id: i32, next: Option<ActiveDownloadSnapshot>) {
        self.runtime
            .mutate(|runtime| {
                runtime
                    .active_downloads
                    .retain(|download| download.worker_id != worker_id);
                if let Some(next) = next {
                    runtime.active_downloads.push(next);
                    runtime
                        .active_downloads
                        .sort_by_key(|download| download.worker_id);
                }
            })
            .await;
    }
}

async fn build_client(paths: &AppPaths, settings: &AppSettings) -> Result<Client> {
    let builder = Client::builder()
        .server_name_or_homeserver_url(settings.homeserver_url.clone())
        .sqlite_store_with_cache_path(&paths.matrix_data_path, &paths.matrix_cache_path, None)
        .user_agent("MatrixMediaArchiver/0.1")
        .sliding_sync_version_builder(matrix_sdk::sliding_sync::VersionBuilder::DiscoverNative);

    Ok(builder.build().await?)
}

async fn connect_client(
    paths: &AppPaths,
    secret_store: &SecretStore,
    settings: &AppSettings,
    password: &str,
) -> Result<Client> {
    if let Some(stored_session) = secret_store.load_session()? {
        if stored_session.homeserver_url == settings.homeserver_url
            && stored_session_matches_settings_login(&stored_session, settings)
        {
            let client = build_client(paths, settings).await?;
            if let Ok(matrix_session) = stored_session.try_into_matrix_session() {
                if client.restore_session(matrix_session).await.is_ok() {
                    return Ok(client);
                }
            }
            drop(client);
        }
        secret_store.clear_session()?;
    }

    reset_matrix_store(paths).await?;
    let client = build_client(paths, settings).await?;
    client
        .matrix_auth()
        .login_username(&settings.username, password)
        .initial_device_display_name("Matrix Media Archiver")
        .await?;
    Ok(client)
}

async fn persist_current_session(secret_store: &SecretStore, client: &Client) -> Result<()> {
    let Some(session) = client.session() else {
        return Ok(());
    };
    let stored = StoredSession::from_auth_session(session, client.homeserver().to_string());
    secret_store.save_session(&stored)?;
    Ok(())
}

fn stored_session_matches_settings_login(
    stored_session: &StoredSession,
    settings: &AppSettings,
) -> bool {
    let normalized_username = settings.username.trim();
    if normalized_username.is_empty() {
        return false;
    }

    if stored_session.user_id == normalized_username {
        return true;
    }

    let trimmed = normalized_username.trim_start_matches('@');
    let localpart = trimmed.split(':').next().unwrap_or(trimmed);
    UserId::parse(stored_session.user_id.as_str())
        .map(|user_id| user_id.localpart() == localpart)
        .unwrap_or(false)
}

fn runtime_user_id_from_settings(settings: &AppSettings) -> Option<String> {
    let normalized_username = settings.username.trim();
    if normalized_username.is_empty() {
        return None;
    }

    if normalized_username.starts_with('@') {
        return Some(normalized_username.to_owned());
    }

    let trimmed = normalized_username.trim_start_matches('@');
    if trimmed.contains(':') {
        return Some(format!("@{trimmed}"));
    }

    let homeserver_host = reqwest::Url::parse(&settings.homeserver_url)
        .ok()
        .and_then(|url| url.host_str().map(ToOwned::to_owned));

    match homeserver_host {
        Some(host) if !host.is_empty() => Some(format!("@{trimmed}:{host}")),
        _ => Some(trimmed.to_owned()),
    }
}

async fn reset_matrix_store(paths: &AppPaths) -> Result<()> {
    remove_directory_if_exists(&paths.matrix_data_path).await?;
    remove_directory_if_exists(&paths.matrix_cache_path).await?;
    paths.ensure_directories()?;
    Ok(())
}

async fn remove_directory_if_exists(path: &Path) -> Result<()> {
    match tokio_fs::remove_dir_all(path).await {
        Ok(()) => Ok(()),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(()),
        Err(error) => {
            Err(error).with_context(|| format!("Failed to remove directory {}", path.display()))
        }
    }
}

async fn watch_sync_state(context: Arc<RunningContext>) {
    let mut subscriber = context.sync_service.state();
    publish_sync_state(&context, &subscriber.get()).await;

    while let Some(state) = subscriber.next().await {
        publish_sync_state(&context, &state).await;
        if matches!(state, SyncState::Offline | SyncState::Error(_)) {
            let _ = context
                .database
                .insert_log(
                    AppLogLevel::Warning,
                    "matrix",
                    "Matrix sync entered an error/offline state.",
                )
                .await;
        }
    }
}

async fn publish_sync_state(context: &Arc<RunningContext>, state: &SyncState) {
    let connection_state = connection_state_from_sync_state(state);
    context
        .runtime
        .mutate(|runtime| runtime.connection_state = connection_state)
        .await;
}

fn connection_state_from_sync_state(state: &SyncState) -> ConnectionState {
    match state {
        SyncState::Idle => ConnectionState::Starting,
        SyncState::Running => ConnectionState::Running,
        SyncState::Terminated => ConnectionState::Stopped,
        SyncState::Offline | SyncState::Error(_) => ConnectionState::Error,
    }
}

async fn watch_session_changes(context: Arc<RunningContext>) {
    let mut receiver = context.client.subscribe_to_session_changes();
    loop {
        match receiver.recv().await {
            Ok(matrix_sdk::SessionChange::TokensRefreshed) => {
                let _ = persist_current_session(&context.secret_store, &context.client).await;
            }
            Ok(matrix_sdk::SessionChange::UnknownToken { soft_logout }) => {
                let _ = context
                    .database
                    .insert_log(
                        AppLogLevel::Error,
                        "matrix",
                        &format!("Authentication error received. softLogout={soft_logout}"),
                    )
                    .await;
                context
                    .runtime
                    .mutate(|runtime| runtime.connection_state = ConnectionState::Error)
                    .await;
            }
            Err(_) => break,
        }
    }
}

async fn watch_verification_state(context: Arc<RunningContext>) {
    let mut subscriber = context.client.encryption().verification_state();
    while subscriber.next().await.is_some() {
        let _ = refresh_verification_snapshot(&context).await;
    }
}

async fn periodic_room_refresh(context: Arc<RunningContext>) {
    loop {
        let connection_state = context.runtime.snapshot().await.connection_state;
        if matches!(
            connection_state,
            ConnectionState::Running | ConnectionState::Starting
        ) {
            let _ = refresh_joined_rooms(context.clone()).await;
            let _ = prune_expired_failed_jobs(&context).await;
        }
        sleep(ROOM_REFRESH_INTERVAL).await;
    }
}

fn schedule_room_refresh(context: Arc<RunningContext>) {
    tokio::spawn(async move {
        let _ = refresh_joined_rooms(context).await;
    });
}

async fn refresh_joined_rooms(context: Arc<RunningContext>) -> Result<()> {
    let settings = context.settings.read().await.clone();
    let current_user_id = runtime_user_id_from_settings(&settings).unwrap_or_default();

    if current_user_id != settings.owner_user_id {
        let invited_rooms = context.client.invited_rooms();
        for room in invited_rooms {
            let _ = accept_owner_invite_if_allowed(&context, &room, &settings.owner_user_id).await;
        }
    }

    let joined_rooms = context.client.joined_rooms();
    let joined_ids = joined_rooms
        .iter()
        .map(|room| room.room_id().to_string())
        .collect::<HashSet<_>>();

    for room in joined_rooms {
        let record = context.room_catalog.sync_sdk_room(&room, &settings).await?;
        ensure_room_worker(context.clone(), room, record.is_space).await?;
    }

    let stale_room_ids = {
        let room_workers = context.room_workers.lock().await;
        room_workers
            .keys()
            .filter(|room_id| !joined_ids.contains(*room_id))
            .cloned()
            .collect::<Vec<_>>()
    };

    for room_id in stale_room_ids {
        cleanup_tracked_space_if_needed(context.clone(), &room_id).await?;
        stop_room_worker(&context, &room_id).await;
    }

    publish_worker_states(&context).await;
    Ok(())
}

async fn ensure_room_worker(
    context: Arc<RunningContext>,
    room: Room,
    is_space: bool,
) -> Result<()> {
    let room_id = room.room_id().to_string();
    let mut workers = context.room_workers.lock().await;
    let entry = workers
        .entry(room_id.clone())
        .or_insert_with(RoomWorkerState::new);

    if !is_space && entry.live_task.is_none() {
        let room_id_clone = room_id.clone();
        let room_clone = room.clone();
        let context_clone = context.clone();
        entry.live_watcher_active = true;
        entry.live_task = Some(tokio::spawn(async move {
            if let Err(error) = run_live_watcher(context_clone.clone(), room_clone).await {
                let _ = context_clone
                    .database
                    .insert_log(
                        AppLogLevel::Error,
                        "rooms",
                        &format!("Live watcher failed for {room_id_clone}: {error:#}"),
                    )
                    .await;
            }
            let mut workers = context_clone.room_workers.lock().await;
            if let Some(worker) = workers.get_mut(&room_id_clone) {
                worker.live_watcher_active = false;
                worker.live_task = None;
            }
            drop(workers);
            publish_worker_states(&context_clone).await;
        }));
    }

    if entry.history_task.is_none() {
        let room_id_clone = room_id.clone();
        let room_clone = room.clone();
        let context_clone = context.clone();
        entry.history_task = Some(tokio::spawn(async move {
            if let Err(error) = run_history_task(context_clone.clone(), room_clone).await {
                let _ = context_clone
                    .database
                    .insert_log(
                        AppLogLevel::Error,
                        "history",
                        &format!("History worker failed for {room_id_clone}: {error:#}"),
                    )
                    .await;
                set_history_state(
                    &context_clone,
                    &room_id_clone,
                    RoomHistoryMode::Idle,
                    "Error",
                )
                .await;
            }

            let mut workers = context_clone.room_workers.lock().await;
            if let Some(worker) = workers.get_mut(&room_id_clone) {
                worker.history_task = None;
            }
            drop(workers);
            publish_worker_states(&context_clone).await;
        }));
    }

    drop(workers);
    publish_worker_states(&context).await;
    Ok(())
}

async fn stop_all_room_workers(context: &Arc<RunningContext>) {
    let room_ids = {
        let workers = context.room_workers.lock().await;
        workers.keys().cloned().collect::<Vec<_>>()
    };
    for room_id in room_ids {
        stop_room_worker(context, &room_id).await;
    }
    publish_worker_states(context).await;
}

async fn stop_room_worker(context: &Arc<RunningContext>, room_id: &str) {
    let worker = {
        let mut workers = context.room_workers.lock().await;
        workers.remove(room_id)
    };

    if let Some(mut worker) = worker {
        if let Some(handle) = worker.live_task.take() {
            handle.abort();
        }
        if let Some(handle) = worker.history_task.take() {
            handle.abort();
        }
    }
}

async fn publish_worker_states(context: &Arc<RunningContext>) {
    let snapshots = {
        let workers = context.room_workers.lock().await;
        let mut snapshots = workers
            .iter()
            .map(|(room_id, worker)| worker.snapshot(room_id))
            .collect::<Vec<_>>();
        snapshots.sort_by(|left, right| left.room_id.cmp(&right.room_id));
        snapshots
    };

    context
        .runtime
        .mutate(|runtime| runtime.worker_states = snapshots)
        .await;
}

async fn set_history_state(
    context: &Arc<RunningContext>,
    room_id: &str,
    mode: RoomHistoryMode,
    detail: &str,
) {
    {
        let mut workers = context.room_workers.lock().await;
        if let Some(worker) = workers.get_mut(room_id) {
            worker.history_mode = mode;
            worker.history_detail = detail.to_owned();
        }
    }
    publish_worker_states(context).await;
}

async fn run_live_watcher(context: Arc<RunningContext>, room: Room) -> Result<()> {
    let room_id = room.room_id().to_string();
    let timeline = room.timeline().await?;
    let (initial_items, mut stream) = timeline.subscribe().await;

    let initial_events = collect_events_from_vector(&initial_items, &room_id)?;
    process_events(
        &context,
        &room_id,
        TimelineSource::Live,
        initial_events,
        None,
    )
    .await?;

    while let Some(diffs) = stream.next().await {
        let events = collect_events_from_diffs(&diffs, &room_id)?;
        process_events(&context, &room_id, TimelineSource::Live, events, None).await?;
    }

    Ok(())
}

async fn run_history_task(context: Arc<RunningContext>, room: Room) -> Result<()> {
    let room_id = room.room_id().to_string();
    let checkpoint = context.database.load_checkpoint(&room_id).await?;
    let hierarchy_snapshot = fetch_room_hierarchy_snapshot(&context.client, &room_id)
        .await
        .ok();
    let is_space_room = hierarchy_snapshot
        .as_ref()
        .is_some_and(|snapshot| snapshot.is_space)
        || room.is_space()
        || context
            .database
            .room_record(&room_id)
            .await?
            .map(|record| record.is_space)
            .unwrap_or(false);

    if is_space_room {
        stop_live_watcher(&context, &room_id).await;
        set_history_state(
            &context,
            &room_id,
            RoomHistoryMode::InitialBackfill,
            "Scanning space rooms",
        )
        .await;
        let membership_changed =
            run_space_refresh(&context, &room_id, checkpoint, hierarchy_snapshot).await?;
        if membership_changed {
            schedule_room_refresh(context.clone());
        }
        return Ok(());
    }

    if checkpoint.initial_backfill_complete {
        set_history_state(
            &context,
            &room_id,
            RoomHistoryMode::ReconnectCatchUp,
            "Recovering missed messages",
        )
        .await;
        run_reconnect_catchup(&context, &room, checkpoint).await?;
    } else {
        set_history_state(
            &context,
            &room_id,
            RoomHistoryMode::InitialBackfill,
            "Scanning room history",
        )
        .await;
        run_initial_backfill(&context, &room).await?;
    }

    set_history_state(&context, &room_id, RoomHistoryMode::Complete, "Idle").await;
    Ok(())
}

async fn stop_live_watcher(context: &Arc<RunningContext>, room_id: &str) {
    let handle = {
        let mut workers = context.room_workers.lock().await;
        workers.get_mut(room_id).and_then(|worker| {
            worker.live_watcher_active = false;
            worker.live_task.take()
        })
    };

    if let Some(handle) = handle {
        handle.abort();
    }

    publish_worker_states(context).await;
}

async fn run_initial_backfill(context: &Arc<RunningContext>, room: &Room) -> Result<()> {
    let room_id = room.room_id().to_string();
    let settings = context.settings.read().await.clone();
    let timeline = room.timeline().await?;
    let (_initial_items, mut stream) = timeline.subscribe().await;
    let mut checkpoint;

    loop {
        drain_timeline_stream(
            context,
            &room_id,
            &mut stream,
            TimelineSource::InitialBackfill,
            None,
        )
        .await?;

        let reached_start = timeline.paginate_backwards(100).await?;
        drain_timeline_stream(
            context,
            &room_id,
            &mut stream,
            TimelineSource::InitialBackfill,
            None,
        )
        .await?;

        checkpoint = context.database.load_checkpoint(&room_id).await?;
        set_history_state(
            context,
            &room_id,
            RoomHistoryMode::InitialBackfill,
            &backfill_detail(&checkpoint, &settings),
        )
        .await;

        if reached_start || should_stop_initial_backfill(&checkpoint, &settings) {
            checkpoint.initial_backfill_complete = true;
            checkpoint.last_history_mode = RoomHistoryMode::Complete;
            checkpoint.last_history_run_at = Some(Utc::now());
            context.database.save_checkpoint(&checkpoint).await?;
            break;
        }
    }

    Ok(())
}

async fn run_reconnect_catchup(
    context: &Arc<RunningContext>,
    room: &Room,
    checkpoint: RoomCheckpoint,
) -> Result<()> {
    let room_id = room.room_id().to_string();
    let timeline = room.timeline().await?;
    let (initial_items, mut stream) = timeline.subscribe().await;
    let initial_events = collect_events_from_vector(&initial_items, &room_id)?;
    process_events(
        context,
        &room_id,
        TimelineSource::ReconnectCatchUp,
        initial_events,
        checkpoint.last_processed_timestamp,
    )
    .await?;

    for _ in 0..10 {
        drain_timeline_stream(
            context,
            &room_id,
            &mut stream,
            TimelineSource::ReconnectCatchUp,
            checkpoint.last_processed_timestamp,
        )
        .await?;
        let reached_start = timeline.paginate_backwards(100).await?;
        drain_timeline_stream(
            context,
            &room_id,
            &mut stream,
            TimelineSource::ReconnectCatchUp,
            checkpoint.last_processed_timestamp,
        )
        .await?;
        if reached_start {
            break;
        }
    }

    Ok(())
}

async fn drain_timeline_stream<S>(
    context: &Arc<RunningContext>,
    room_id: &str,
    stream: &mut S,
    source: TimelineSource,
    cutoff: Option<DateTime<Utc>>,
) -> Result<()>
where
    S: futures_util::Stream<Item = Vec<VectorDiff<Arc<TimelineItem>>>> + Unpin,
{
    while let Ok(Some(diffs)) = tokio::time::timeout(Duration::from_millis(50), stream.next()).await
    {
        let events = collect_events_from_diffs(&diffs, room_id)?;
        process_events(context, room_id, source, events, cutoff).await?;
    }
    Ok(())
}

async fn run_space_refresh(
    context: &Arc<RunningContext>,
    room_id: &str,
    mut checkpoint: RoomCheckpoint,
    hierarchy_snapshot: Option<RoomHierarchySnapshot>,
) -> Result<bool> {
    let settings = context.settings.read().await.clone();
    let snapshot = match hierarchy_snapshot {
        Some(snapshot) => snapshot,
        None => fetch_room_hierarchy_snapshot(&context.client, room_id).await?,
    };
    context
        .room_catalog
        .sync_hierarchy_metadata(
            room_id,
            snapshot.display_name.clone(),
            snapshot.canonical_alias.clone(),
            "joined",
            &settings,
        )
        .await?;
    let membership_changed = reconcile_space_children(context, room_id, &snapshot.children).await?;

    checkpoint.initial_backfill_complete = true;
    checkpoint.last_history_mode = RoomHistoryMode::Complete;
    checkpoint.last_history_run_at = Some(Utc::now());
    context.database.save_checkpoint(&checkpoint).await?;

    let detail = if snapshot.children.len() == 1 {
        "Tracking 1 space room".to_owned()
    } else {
        format!("Tracking {} space rooms", snapshot.children.len())
    };
    set_history_state(context, room_id, RoomHistoryMode::Complete, &detail).await;
    Ok(membership_changed)
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn connection_state_mapping_matches_swift_port_behavior() {
        assert_eq!(
            connection_state_from_sync_state(&SyncState::Idle),
            ConnectionState::Starting
        );
        assert_eq!(
            connection_state_from_sync_state(&SyncState::Running),
            ConnectionState::Running
        );
        assert_eq!(
            connection_state_from_sync_state(&SyncState::Terminated),
            ConnectionState::Stopped
        );
    }

    #[test]
    fn stored_session_restore_only_matches_same_login_identity() {
        let stored_session = StoredSession {
            access_token: "token".to_owned(),
            refresh_token: None,
            user_id: "@meow:fantasyhaven.me".to_owned(),
            device_id: "DEVICE".to_owned(),
            homeserver_url: "https://fantasyhaven.me".to_owned(),
            sliding_sync_version: None,
        };

        let exact_settings = AppSettings {
            homeserver_url: "https://fantasyhaven.me".to_owned(),
            username: "@meow:fantasyhaven.me".to_owned(),
            owner_user_id: String::new(),
            destination_root_path: String::new(),
            message_limit: 0,
            time_window_value: 0,
            time_window_unit: crate::domain::TimeWindowUnit::Day,
            retry_cooldown_minutes: 0,
            retry_limit: 0,
            download_worker_count: 0,
            failed_job_retention_value: 0,
            failed_job_retention_unit: crate::domain::FailedJobRetentionUnit::Day,
            desired_power_state: true,
        };
        let localpart_settings = AppSettings {
            username: "meow".to_owned(),
            ..exact_settings.clone()
        };
        let different_user_settings = AppSettings {
            username: "someoneelse".to_owned(),
            ..exact_settings.clone()
        };

        assert!(stored_session_matches_settings_login(
            &stored_session,
            &exact_settings
        ));
        assert!(stored_session_matches_settings_login(
            &stored_session,
            &localpart_settings
        ));
        assert!(!stored_session_matches_settings_login(
            &stored_session,
            &different_user_settings
        ));
    }

    #[test]
    fn runtime_user_id_from_settings_preserves_full_matrix_id() {
        let settings = AppSettings {
            homeserver_url: "https://fantasyhaven.me".to_owned(),
            username: "@meow:fantasyhaven.me".to_owned(),
            owner_user_id: String::new(),
            destination_root_path: String::new(),
            message_limit: 0,
            time_window_value: 0,
            time_window_unit: crate::domain::TimeWindowUnit::Day,
            retry_cooldown_minutes: 0,
            retry_limit: 0,
            download_worker_count: 0,
            failed_job_retention_value: 0,
            failed_job_retention_unit: crate::domain::FailedJobRetentionUnit::Day,
            desired_power_state: true,
        };

        assert_eq!(
            runtime_user_id_from_settings(&settings).as_deref(),
            Some("@meow:fantasyhaven.me")
        );
    }

    #[test]
    fn runtime_user_id_from_settings_expands_localpart_with_homeserver() {
        let settings = AppSettings {
            homeserver_url: "https://fantasyhaven.me".to_owned(),
            username: "meow".to_owned(),
            owner_user_id: String::new(),
            destination_root_path: String::new(),
            message_limit: 0,
            time_window_value: 0,
            time_window_unit: crate::domain::TimeWindowUnit::Day,
            retry_cooldown_minutes: 0,
            retry_limit: 0,
            download_worker_count: 0,
            failed_job_retention_value: 0,
            failed_job_retention_unit: crate::domain::FailedJobRetentionUnit::Day,
            desired_power_state: true,
        };

        assert_eq!(
            runtime_user_id_from_settings(&settings).as_deref(),
            Some("@meow:fantasyhaven.me")
        );
    }

    #[test]
    fn hierarchy_response_tolerates_rooms_missing_room_id() {
        let response: SpaceHierarchyResponse = serde_json::from_value(json!({
            "rooms": [
                {
                    "room_id": "!space:example.org",
                    "room_type": "m.space",
                    "name": "Example Space",
                    "children_state": [
                        {
                            "type": "m.space.child",
                            "state_key": "!child:example.org",
                            "content": { "via": ["example.org"] }
                        }
                    ]
                },
                {
                    "name": "Malformed Child"
                }
            ]
        }))
        .expect("hierarchy response should deserialize");

        assert_eq!(response.rooms.len(), 2);
        assert_eq!(
            response.rooms[0].room_id.as_deref(),
            Some("!space:example.org")
        );
        assert_eq!(response.rooms[1].room_id, None);
    }
}

async fn process_events(
    context: &Arc<RunningContext>,
    room_id: &str,
    source: TimelineSource,
    events: Vec<ObservedTimelineEvent>,
    cutoff: Option<DateTime<Utc>>,
) -> Result<()> {
    let mut event_count = 0;
    let mut oldest: Option<(String, DateTime<Utc>)> = None;
    let mut newest: Option<(String, DateTime<Utc>)> = None;
    let is_space_room = context
        .database
        .room_record(room_id)
        .await?
        .map(|record| record.is_space)
        .unwrap_or(false);

    for event in events {
        if cutoff.is_some_and(|cutoff| event.timestamp <= cutoff)
            && source == TimelineSource::ReconnectCatchUp
        {
            continue;
        }

        event_count += 1;
        if oldest.as_ref().is_none_or(|(_, ts)| event.timestamp < *ts) {
            oldest = Some((event.event_id.clone(), event.timestamp));
        }
        if newest.as_ref().is_none_or(|(_, ts)| event.timestamp >= *ts) {
            newest = Some((event.event_id.clone(), event.timestamp));
        }

        if !is_space_room && let Some(discovery) = event.discovery {
            context.database.enqueue_discovery(&discovery).await?;
        }

        if source != TimelineSource::InitialBackfill {
            if let Some(body) = event.command_body {
                handle_owner_command(context, &event.event_id, &event.sender, &body, room_id)
                    .await?;
            }
        }
    }

    if event_count == 0 {
        return Ok(());
    }

    let mut checkpoint = context.database.load_checkpoint(room_id).await?;
    match source {
        TimelineSource::Live | TimelineSource::ReconnectCatchUp => {
            if let Some((event_id, timestamp)) = newest {
                if checkpoint
                    .last_processed_timestamp
                    .is_none_or(|current| timestamp >= current)
                {
                    checkpoint.last_processed_event_id = Some(event_id);
                    checkpoint.last_processed_timestamp = Some(timestamp);
                }
            }
        }
        TimelineSource::InitialBackfill => {
            checkpoint.historical_message_count += event_count;
            if let Some((event_id, timestamp)) = oldest {
                if checkpoint
                    .oldest_backfilled_timestamp
                    .is_none_or(|current| timestamp < current)
                {
                    checkpoint.oldest_backfilled_event_id = Some(event_id);
                    checkpoint.oldest_backfilled_timestamp = Some(timestamp);
                }
            }
            checkpoint.last_history_mode = RoomHistoryMode::InitialBackfill;
            checkpoint.last_history_run_at = Some(Utc::now());
        }
    }

    context.database.save_checkpoint(&checkpoint).await?;
    Ok(())
}

async fn handle_owner_command(
    context: &Arc<RunningContext>,
    event_id: &str,
    sender: &str,
    body: &str,
    room_id: &str,
) -> Result<()> {
    let settings = context.settings.read().await.clone();
    if sender != settings.owner_user_id {
        return Ok(());
    }

    {
        let mut handled = context.handled_event_ids.lock().await;
        if !handled.insert(event_id.to_owned()) {
            return Ok(());
        }
    }

    let trimmed = body.trim();
    if !trimmed.starts_with("!matrixdl ") {
        return Ok(());
    }

    let mut components = trimmed.split_whitespace();
    let _ = components.next();
    let Some(command) = components.next() else {
        return Ok(());
    };
    if !command.eq_ignore_ascii_case("join") {
        return Ok(());
    }

    let target = components.collect::<Vec<_>>().join(" ");
    if target.trim().is_empty() {
        return Ok(());
    }

    context
        .database
        .insert_log(
            AppLogLevel::Info,
            "commands",
            &format!("Command from {sender} in {room_id}: {body}"),
        )
        .await?;

    match join_room(context, target.trim(), &[]).await {
        Ok(()) => {
            schedule_room_refresh(context.clone());
            context
                .database
                .insert_log(
                    AppLogLevel::Info,
                    "commands",
                    &format!("Followed join command: {}", target.trim()),
                )
                .await?;
            if context.client.user_id().map(|user_id| user_id.as_str())
                != Some(settings.owner_user_id.as_str())
            {
                let _ = send_owner_reply(context, "Joined", target.trim()).await;
            }
        }
        Err(error) => {
            context
                .database
                .insert_log(
                    AppLogLevel::Error,
                    "commands",
                    &format!("Join command failed for {}: {error:#}", target.trim()),
                )
                .await?;
            if context.client.user_id().map(|user_id| user_id.as_str())
                != Some(settings.owner_user_id.as_str())
            {
                let _ = send_owner_reply(
                    context,
                    "Join failed for",
                    &format!("{}: {error:#}", target.trim()),
                )
                .await;
            }
        }
    }

    Ok(())
}

async fn send_owner_reply(context: &Arc<RunningContext>, prefix: &str, detail: &str) -> Result<()> {
    let settings = context.settings.read().await.clone();
    let owner_id = UserId::parse(settings.owner_user_id)?;
    let room = if let Some(room) = context.client.get_dm_room(&owner_id) {
        room
    } else {
        context.client.create_dm(&owner_id).await?
    };
    let timeline = room.timeline().await?;
    let content = RoomMessageEventContent::text_plain(format!("{prefix} {detail}"));
    timeline.send(content.into()).await?;
    Ok(())
}

async fn request_verification(context: &Arc<RunningContext>) -> Result<()> {
    let device = context
        .client
        .encryption()
        .get_own_device()
        .await?
        .ok_or_else(|| anyhow!("Unable to find the current device for verification"))?;
    let request = device.request_verification().await?;

    let mut verification = context.verification.lock().await;
    if let Some(task) = verification.sas_task.take() {
        task.abort();
    }
    verification.request = Some(request);
    verification.sas = None;
    drop(verification);

    refresh_verification_snapshot(context).await?;
    Ok(())
}

async fn start_sas_verification(context: &Arc<RunningContext>) -> Result<()> {
    let request = context
        .verification
        .lock()
        .await
        .request
        .clone()
        .ok_or_else(|| anyhow!("No verification request is active."))?;

    let sas = request
        .start_sas()
        .await?
        .ok_or_else(|| anyhow!("Verification request is not ready for SAS yet."))?;

    let watcher_context = context.clone();
    let watcher_sas = sas.clone();
    let watcher = tokio::spawn(async move {
        let mut changes = watcher_sas.changes();
        while changes.next().await.is_some() {
            let _ = refresh_verification_snapshot(&watcher_context).await;
        }
    });

    let mut verification = context.verification.lock().await;
    if let Some(task) = verification.sas_task.take() {
        task.abort();
    }
    verification.sas = Some(sas);
    verification.sas_task = Some(watcher);
    drop(verification);

    refresh_verification_snapshot(context).await?;
    Ok(())
}

async fn approve_verification(context: &Arc<RunningContext>) -> Result<()> {
    let sas = context
        .verification
        .lock()
        .await
        .sas
        .clone()
        .ok_or_else(|| anyhow!("No SAS verification is active."))?;
    sas.confirm().await?;
    refresh_verification_snapshot(context).await?;
    Ok(())
}

async fn decline_verification(context: &Arc<RunningContext>) -> Result<()> {
    let (sas, request) = {
        let verification = context.verification.lock().await;
        (verification.sas.clone(), verification.request.clone())
    };

    if let Some(sas) = sas {
        sas.mismatch().await?;
    } else if let Some(request) = request {
        request.cancel().await?;
    } else {
        return Err(anyhow!("No verification flow is active."));
    }

    refresh_verification_snapshot(context).await?;
    Ok(())
}

async fn refresh_verification_snapshot(context: &Arc<RunningContext>) -> Result<()> {
    let state = context.client.encryption().verification_state().get();
    let status = match state {
        VerificationState::Unknown => VerificationStatus::Unknown,
        VerificationState::Verified => VerificationStatus::Verified,
        VerificationState::Unverified => VerificationStatus::Unverified,
    };

    let (emojis, decimals) = {
        let verification = context.verification.lock().await;
        if let Some(sas) = &verification.sas {
            let emojis = sas
                .emoji()
                .map(|values| {
                    values
                        .iter()
                        .map(|emoji| VerificationEmoji {
                            symbol: emoji.symbol.to_owned(),
                            description: emoji.description.to_owned(),
                        })
                        .collect::<Vec<_>>()
                })
                .unwrap_or_default();
            let decimals = sas
                .decimals()
                .map(|values| vec![values.0, values.1, values.2])
                .unwrap_or_default();
            (emojis, decimals)
        } else {
            (Vec::new(), Vec::new())
        }
    };

    let device_id = context.client.device_id().map(ToString::to_string);
    context
        .runtime
        .mutate(|runtime| {
            runtime.verification = VerificationSnapshot {
                state: status,
                device_id: device_id.clone(),
                emojis: emojis.clone(),
                decimals: decimals.clone(),
            };
        })
        .await;
    Ok(())
}

async fn accept_owner_invite_if_allowed(
    context: &Arc<RunningContext>,
    room: &Room,
    owner_user_id: &str,
) -> Result<bool> {
    let invite = room.invite_details().await?;
    let Some(inviter) = invite.inviter else {
        return Ok(false);
    };
    if inviter.user_id() != owner_user_id {
        context
            .database
            .insert_log(
                AppLogLevel::Info,
                "invites",
                &format!(
                    "Ignoring invite to {} from non-owner {}",
                    room.room_id(),
                    inviter.user_id()
                ),
            )
            .await?;
        return Ok(false);
    }

    room.join().await?;
    context
        .database
        .insert_log(
            AppLogLevel::Info,
            "invites",
            &format!("Accepted invite to {} from owner", room.room_id()),
        )
        .await?;
    Ok(true)
}

async fn join_room(
    context: &Arc<RunningContext>,
    room_id_or_alias: &str,
    via_servers: &[String],
) -> Result<()> {
    if !room_id_or_alias.starts_with('!') && !room_id_or_alias.starts_with('#') {
        return Err(anyhow!(
            "Room joins require a Matrix room alias (#room:server) or room ID (!id:server)."
        ));
    }

    let room_id_or_alias = RoomOrAliasId::parse(room_id_or_alias)?;
    let via = if !via_servers.is_empty() {
        via_servers
            .iter()
            .filter_map(|server| ServerName::parse(server.as_str()).ok())
            .map(OwnedServerName::from)
            .collect::<Vec<_>>()
    } else if let Ok(alias) = RoomAliasId::parse(room_id_or_alias.as_str()) {
        context
            .client
            .resolve_room_alias(&alias)
            .await
            .map(|response| response.servers)
            .unwrap_or_default()
    } else {
        Vec::new()
    };

    context
        .client
        .join_room_by_id_or_alias(room_id_or_alias.as_ref(), &via)
        .await?;
    context
        .database
        .insert_log(
            AppLogLevel::Info,
            "rooms",
            &format!("Joined room {}", room_id_or_alias.as_str()),
        )
        .await?;
    Ok(())
}

async fn leave_room(context: &Arc<RunningContext>, room_id: &str) -> Result<()> {
    let room = context
        .client
        .joined_rooms()
        .into_iter()
        .find(|room| room.room_id().as_str() == room_id)
        .ok_or_else(|| anyhow!("Room not found: {room_id}"))?;

    let tracked_links = context
        .database
        .fetch_space_auto_join_links_for_space(room_id)
        .await?;
    room.leave().await?;
    context
        .database
        .insert_log(AppLogLevel::Info, "rooms", &format!("Left room {room_id}"))
        .await?;

    if !tracked_links.is_empty() {
        let _ = cleanup_tracked_space(context.clone(), room_id, "user left the space").await?;
    }

    Ok(())
}

async fn cleanup_tracked_space_if_needed(
    context: Arc<RunningContext>,
    parent_space_id: &str,
) -> Result<()> {
    let _ = cleanup_tracked_space(context, parent_space_id, "space is no longer joined").await?;
    Ok(())
}

async fn cleanup_tracked_space(
    context: Arc<RunningContext>,
    parent_space_id: &str,
    reason: &str,
) -> Result<bool> {
    let child_links = context
        .database
        .fetch_space_auto_join_links_for_space(parent_space_id)
        .await?;
    if child_links.is_empty() {
        return Ok(false);
    }
    let child_link_count = child_links.len();

    let mut membership_changed = false;
    for link in child_links {
        context
            .database
            .delete_space_auto_join_link(&link.space_room_id, &link.child_room_id)
            .await?;
        if link.auto_joined_by_bot {
            membership_changed |= maybe_leave_orphaned_auto_joined_room(
                context.clone(),
                &link.child_room_id,
                parent_space_id,
            )
            .await?;
        }
    }

    context
        .database
        .insert_log(
            AppLogLevel::Info,
            "rooms",
            &format!(
                "Stopped tracking {} space rooms from {}: {}",
                child_link_count, parent_space_id, reason
            ),
        )
        .await?;

    Ok(membership_changed)
}

async fn maybe_leave_orphaned_auto_joined_room(
    context: Arc<RunningContext>,
    room_id: &str,
    parent_space_id: &str,
) -> Result<bool> {
    let remaining_links = context
        .database
        .fetch_space_auto_join_links_for_child(room_id)
        .await?;
    if !remaining_links.is_empty() {
        return Ok(false);
    }

    let joined_room = context
        .client
        .joined_rooms()
        .into_iter()
        .find(|room| room.room_id().as_str() == room_id);
    if let Some(room) = joined_room {
        room.leave().await?;
        context
            .database
            .insert_log(
                AppLogLevel::Info,
                "rooms",
                &format!(
                    "Left {} after it was removed from space {}",
                    room_id, parent_space_id
                ),
            )
            .await?;
        return Ok(true);
    }

    Ok(false)
}

async fn reconcile_space_children(
    context: &Arc<RunningContext>,
    parent_space_id: &str,
    children: &[SpaceChildDescriptor],
) -> Result<bool> {
    let unique_children = children
        .iter()
        .filter(|child| child.room_id != parent_space_id && child.room_id.starts_with('!'))
        .cloned()
        .fold(
            HashMap::<String, SpaceChildDescriptor>::new(),
            |mut map, child| {
                map.entry(child.room_id.clone()).or_insert(child);
                map
            },
        );
    let current_child_ids = unique_children.keys().cloned().collect::<HashSet<_>>();
    let existing_links = context
        .database
        .fetch_space_auto_join_links_for_space(parent_space_id)
        .await?;
    let mut joined_room_ids = context
        .client
        .joined_rooms()
        .into_iter()
        .map(|room| room.room_id().to_string())
        .collect::<HashSet<_>>();
    let mut membership_changed = false;

    for child in unique_children.values() {
        if joined_room_ids.contains(&child.room_id) {
            context
                .database
                .upsert_space_auto_join_link(parent_space_id, &child.room_id, false)
                .await?;
            continue;
        }

        match join_room(context, &child.room_id, &child.via_servers).await {
            Ok(()) => {
                membership_changed = true;
                joined_room_ids.insert(child.room_id.clone());
                context
                    .database
                    .upsert_space_auto_join_link(parent_space_id, &child.room_id, true)
                    .await?;
                context
                    .database
                    .insert_log(
                        AppLogLevel::Info,
                        "rooms",
                        &format!("Joined {} from space {}", child.room_id, parent_space_id),
                    )
                    .await?;
            }
            Err(error) => {
                context
                    .database
                    .insert_log(
                        AppLogLevel::Warning,
                        "rooms",
                        &format!(
                            "Failed joining {} from space {}: {error:#}",
                            child.room_id, parent_space_id
                        ),
                    )
                    .await?;
            }
        }
    }

    for link in existing_links {
        if current_child_ids.contains(&link.child_room_id) {
            continue;
        }

        context
            .database
            .delete_space_auto_join_link(&link.space_room_id, &link.child_room_id)
            .await?;
        if link.auto_joined_by_bot {
            membership_changed |= maybe_leave_orphaned_auto_joined_room(
                context.clone(),
                &link.child_room_id,
                parent_space_id,
            )
            .await?;
        }
    }

    Ok(membership_changed)
}

async fn fetch_room_hierarchy_snapshot(
    client: &Client,
    room_id: &str,
) -> Result<RoomHierarchySnapshot> {
    let access_token = client
        .access_token()
        .ok_or_else(|| anyhow!("Client is not authenticated"))?;
    let homeserver_url = client.homeserver();
    let encoded_room_id = urlencoding::encode(room_id);
    let endpoint_paths = [
        format!("/_matrix/client/v1/rooms/{encoded_room_id}/hierarchy"),
        format!("/_matrix/client/unstable/org.matrix.msc2946/rooms/{encoded_room_id}/hierarchy"),
    ];

    let http_client = reqwest::Client::builder()
        .timeout(Duration::from_secs(15))
        .build()?;
    let mut from_token: Option<String> = None;
    let mut root_room_type: Option<String> = None;
    let mut root_display_name: Option<String> = None;
    let mut root_canonical_alias: Option<String> = None;
    let mut direct_children: HashMap<String, HashSet<String>> = HashMap::new();
    let mut fallback_children = HashSet::new();
    let mut attempts = Vec::new();

    loop {
        let mut payload = None;
        for endpoint in &endpoint_paths {
            let mut url = homeserver_url.join(endpoint)?;
            {
                let mut pairs = url.query_pairs_mut();
                pairs.append_pair("max_depth", "1");
                pairs.append_pair("limit", "200");
                pairs.append_pair("suggested_only", "false");
                if let Some(token) = &from_token {
                    pairs.append_pair("from", token);
                }
            }

            let response = http_client
                .get(url.clone())
                .header(AUTHORIZATION, format!("Bearer {access_token}"))
                .header(ACCEPT, "application/json")
                .header(USER_AGENT, "MatrixMediaArchiver/0.1")
                .send()
                .await;

            let response = match response {
                Ok(response) => response,
                Err(error) => {
                    attempts.push(format!("{} -> {}", url, error));
                    continue;
                }
            };

            if response.status().as_u16() == 404 || response.status().as_u16() == 405 {
                attempts.push(format!("{} -> HTTP {}", url, response.status()));
                continue;
            }

            if !response.status().is_success() {
                let status = response.status();
                let body = response.text().await.unwrap_or_default();
                return Err(anyhow!(
                    "Space hierarchy request failed: [{}] {}",
                    status,
                    body
                ));
            }

            payload = Some(response.json::<SpaceHierarchyResponse>().await?);
            break;
        }

        let Some(page) = payload else {
            return Err(anyhow!(
                "Space hierarchy request failed: {}",
                attempts.join(" | ")
            ));
        };

        for room in page.rooms {
            let Some(page_room_id) = room.room_id else {
                continue;
            };

            if page_room_id == room_id {
                root_room_type = room.room_type.or(root_room_type);
                root_display_name = room.name.or(root_display_name);
                root_canonical_alias = room.canonical_alias.or(root_canonical_alias);
                for child in room
                    .children_state
                    .into_iter()
                    .filter(|child| child.event_type.as_deref() == Some("m.space.child"))
                {
                    let Some(state_key) = child.state_key else {
                        continue;
                    };
                    if state_key != room_id {
                        direct_children
                            .entry(state_key)
                            .or_default()
                            .extend(child.content.via.unwrap_or_default());
                    }
                }
                continue;
            }

            fallback_children.insert(page_room_id);
        }

        from_token = page.next_batch;
        if from_token.is_none() {
            break;
        }
    }

    let is_space = root_room_type.as_deref() == Some("m.space") || !direct_children.is_empty();
    let children = if direct_children.is_empty() {
        if is_space {
            fallback_children
                .into_iter()
                .filter(|child_room_id| child_room_id != room_id)
                .map(|room_id| SpaceChildDescriptor {
                    room_id,
                    via_servers: Vec::new(),
                })
                .collect::<Vec<_>>()
        } else {
            Vec::new()
        }
    } else {
        direct_children
            .into_iter()
            .map(|(room_id, via_servers)| SpaceChildDescriptor {
                room_id,
                via_servers: via_servers.into_iter().collect(),
            })
            .collect::<Vec<_>>()
    };

    Ok(RoomHierarchySnapshot {
        room_id: room_id.to_owned(),
        is_space,
        display_name: root_display_name,
        canonical_alias: root_canonical_alias,
        children,
    })
}

async fn prune_expired_failed_jobs(context: &Arc<RunningContext>) -> Result<()> {
    let settings = context.settings.read().await.clone();
    let Some(cutoff) = failed_job_cutoff(&settings) else {
        return Ok(());
    };

    let cleared = context.database.prune_permanent_failed_jobs(cutoff).await?;
    if cleared > 0 {
        context
            .database
            .insert_log(
                AppLogLevel::Info,
                "queue",
                &format!("Auto-cleared {cleared} permanently failed jobs."),
            )
            .await?;
    }
    Ok(())
}

async fn download_media_to_temp(
    client: &Client,
    paths: &AppPaths,
    runtime: &RuntimeStore,
    worker_id: i32,
    job: &DownloadJobRecord,
    homeserver_url: String,
) -> Result<PathBuf> {
    let source = decode_media_source(&job.mxc_url)?;
    if let MediaSource::Plain(uri) = &source {
        if is_remote_media(uri.as_str(), &homeserver_url) {
            if let Some(path) = direct_remote_media_download_to_temp(
                runtime,
                &paths.temp_downloads_path,
                worker_id,
                job,
                uri.as_str(),
                &homeserver_url,
                client.access_token(),
            )
            .await?
            {
                return Ok(path);
            }
        }
    }

    let mime = job
        .mime_type
        .as_deref()
        .unwrap_or("application/octet-stream")
        .parse::<Mime>()
        .unwrap_or(mime::APPLICATION_OCTET_STREAM);
    let request = MediaRequestParameters {
        source,
        format: MediaFormat::File,
    };
    let handle = client
        .media()
        .get_media_file(
            &request,
            job.original_filename.clone(),
            &mime,
            false,
            Some(paths.temp_downloads_path.to_string_lossy().to_string()),
        )
        .await?;

    let extension = media_classification::preferred_extension(
        job.original_filename.as_deref(),
        job.mime_type.as_deref(),
    );
    let temp_path = temp_download_path(&paths.temp_downloads_path, extension.as_deref());
    let persisted = handle
        .persist(&temp_path)
        .map_err(|error| anyhow!(error.error))?;
    let size = persisted
        .metadata()
        .map(|value| value.len() as i64)
        .unwrap_or_default();
    runtime
        .mutate(|snapshot| {
            if let Some(active) = snapshot
                .active_downloads
                .iter_mut()
                .find(|download| download.worker_id == worker_id)
            {
                active.received_bytes = size;
                active.total_bytes = Some(size);
            }
        })
        .await;
    Ok(temp_path)
}

async fn direct_remote_media_download_to_temp(
    runtime: &RuntimeStore,
    temp_root: &Path,
    worker_id: i32,
    job: &DownloadJobRecord,
    mxc_url: &str,
    homeserver_url: &str,
    access_token: Option<String>,
) -> Result<Option<PathBuf>> {
    let Some((server_name, media_id)) = parse_mxc_url(mxc_url) else {
        return Ok(None);
    };

    let remote_host = server_name
        .split(':')
        .next()
        .unwrap_or_default()
        .to_ascii_lowercase();
    let homeserver_host = reqwest::Url::parse(homeserver_url)
        .ok()
        .and_then(|url| url.host_str().map(|host| host.to_ascii_lowercase()))
        .unwrap_or_default();
    if remote_host == homeserver_host {
        return Ok(None);
    }

    let base = reqwest::Url::parse(homeserver_url)?;
    let encoded_server = urlencoding::encode(&server_name);
    let encoded_media_id = urlencoding::encode(&media_id);
    let candidate_paths = [
        format!("/_matrix/client/v1/media/download/{encoded_server}/{encoded_media_id}"),
        format!("/_matrix/media/v3/download/{encoded_server}/{encoded_media_id}"),
        format!("/_matrix/media/r0/download/{encoded_server}/{encoded_media_id}"),
    ];

    let http_client = reqwest::Client::builder()
        .timeout(Duration::from_secs(15))
        .build()?;
    for candidate_path in candidate_paths {
        let url = base.join(&candidate_path)?;
        let temp_path = temp_download_path(
            temp_root,
            media_classification::preferred_extension(
                job.original_filename.as_deref(),
                job.mime_type.as_deref(),
            )
            .as_deref(),
        );

        let mut request = http_client
            .get(url.clone())
            .header(USER_AGENT, "MatrixMediaArchiver/0.1");
        if access_token.is_some() && same_origin(&url, &base) {
            request = request.header(
                AUTHORIZATION,
                format!("Bearer {}", access_token.clone().unwrap()),
            );
        }

        let response = match request.send().await {
            Ok(response) => response,
            Err(_) => continue,
        };
        if !response.status().is_success() {
            continue;
        }

        let total = response.content_length().map(|value| value as i64);
        let mut received = 0i64;
        let mut file = tokio_fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&temp_path)
            .await?;
        let mut stream = response.bytes_stream();
        while let Some(chunk) = stream.next().await {
            let chunk = chunk?;
            file.write_all(&chunk).await?;
            received += chunk.len() as i64;
            runtime
                .mutate(|snapshot| {
                    if let Some(active) = snapshot
                        .active_downloads
                        .iter_mut()
                        .find(|download| download.worker_id == worker_id)
                    {
                        active.received_bytes = received;
                        active.total_bytes = total;
                    }
                })
                .await;
        }
        file.flush().await?;
        if tokio_fs::metadata(&temp_path)
            .await
            .map(|meta| meta.len())
            .unwrap_or_default()
            > 0
        {
            return Ok(Some(temp_path));
        }
        let _ = tokio_fs::remove_file(&temp_path).await;
    }

    Ok(None)
}

async fn handle_job_failure(
    database: &AppDatabase,
    settings: &Arc<RwLock<AppSettings>>,
    job: &DownloadJobRecord,
    error: &anyhow::Error,
) -> Result<()> {
    let description = error.to_string();
    let lowered = description.to_ascii_lowercase();
    let settings = settings.read().await.clone();
    let next_eligible_at =
        Utc::now() + chrono::Duration::minutes(i64::from(settings.retry_cooldown_minutes.max(1)));
    if lowered.contains("decrypt") || lowered.contains("utd") {
        database
            .mark_job_undecryptable(job.id, next_eligible_at, &description)
            .await?;
        database
            .insert_log(
                AppLogLevel::Warning,
                "queue",
                &format!(
                    "Marked {} as undecryptable pending keys until {}.",
                    job.original_filename
                        .clone()
                        .unwrap_or_else(|| job.event_id.clone()),
                    next_eligible_at.to_rfc3339_opts(chrono::SecondsFormat::Secs, true),
                ),
            )
            .await?;
        return Ok(());
    }

    let retry_count = job.retry_count + 1;
    let permanently_failed = retry_count >= settings.retry_limit;
    database
        .mark_job_cooling_down(
            job.id,
            retry_count,
            next_eligible_at,
            &description,
            permanently_failed,
        )
        .await?;

    let level = if permanently_failed {
        AppLogLevel::Error
    } else {
        AppLogLevel::Warning
    };
    let message = if permanently_failed {
        format!(
            "{} failed permanently: {}",
            job.original_filename
                .clone()
                .unwrap_or_else(|| job.event_id.clone()),
            description
        )
    } else {
        format!(
            "{} failed. Cooling down until {}: {}",
            job.original_filename
                .clone()
                .unwrap_or_else(|| job.event_id.clone()),
            next_eligible_at,
            description
        )
    };
    database.insert_log(level, "queue", &message).await?;
    Ok(())
}

fn collect_events_from_vector(
    items: &Vector<Arc<TimelineItem>>,
    room_id: &str,
) -> Result<Vec<ObservedTimelineEvent>> {
    items
        .iter()
        .filter_map(|item| ObservedTimelineEvent::from_timeline_item(item, room_id).transpose())
        .collect()
}

fn collect_events_from_diffs(
    diffs: &[VectorDiff<Arc<TimelineItem>>],
    room_id: &str,
) -> Result<Vec<ObservedTimelineEvent>> {
    let mut events = Vec::new();
    for diff in diffs {
        match diff {
            VectorDiff::Append { values } | VectorDiff::Reset { values } => {
                for item in values {
                    if let Some(event) = ObservedTimelineEvent::from_timeline_item(item, room_id)? {
                        events.push(event);
                    }
                }
            }
            VectorDiff::PushBack { value }
            | VectorDiff::PushFront { value }
            | VectorDiff::Insert { value, .. }
            | VectorDiff::Set { value, .. } => {
                if let Some(event) = ObservedTimelineEvent::from_timeline_item(value, room_id)? {
                    events.push(event);
                }
            }
            VectorDiff::Clear
            | VectorDiff::Remove { .. }
            | VectorDiff::PopBack
            | VectorDiff::PopFront
            | VectorDiff::Truncate { .. } => {}
        }
    }
    Ok(events)
}

struct ObservedTimelineEvent {
    event_id: String,
    sender: String,
    timestamp: DateTime<Utc>,
    command_body: Option<String>,
    discovery: Option<AttachmentDiscovery>,
}

impl ObservedTimelineEvent {
    fn from_timeline_item(item: &Arc<TimelineItem>, room_id: &str) -> Result<Option<Self>> {
        let Some(event) = item.as_event() else {
            return Ok(None);
        };
        if !event.is_remote_event() {
            return Ok(None);
        }

        let timestamp = DateTime::from_timestamp_millis(event.timestamp().get().into())
            .ok_or_else(|| anyhow!("Invalid Matrix event timestamp"))?;
        let event_id = event
            .event_id()
            .map(ToString::to_string)
            .unwrap_or_else(|| format!("remote-{}-{}", event.timestamp().get(), event.sender()));
        let sender = event.sender().to_string();
        let command_body = event
            .content()
            .as_message()
            .map(|message| message.body().to_owned());
        let discovery = timeline_discovery(event, room_id, &event_id, timestamp)?;

        Ok(Some(Self {
            event_id,
            sender,
            timestamp,
            command_body,
            discovery,
        }))
    }
}

fn timeline_discovery(
    event: &matrix_sdk_ui::timeline::EventTimelineItem,
    room_id: &str,
    event_id: &str,
    timestamp: DateTime<Utc>,
) -> Result<Option<AttachmentDiscovery>> {
    let Some(message) = event.content().as_message() else {
        if let Some(sticker) = event.content().as_sticker() {
            let content = sticker.content();
            let source: MediaSource = content.source.clone().into();
            return Ok(Some(AttachmentDiscovery {
                room_id: room_id.to_owned(),
                event_id: event_id.to_owned(),
                origin_server_timestamp: timestamp,
                mxc_url: encode_media_source(&source)?,
                original_filename: Some(content.body.clone()),
                mime_type: content.info.mimetype.clone(),
                category: MediaCategory::Images,
            }));
        }
        return Ok(None);
    };

    let discovery = match message.msgtype() {
        MessageType::Image(content) => Some(AttachmentDiscovery {
            room_id: room_id.to_owned(),
            event_id: event_id.to_owned(),
            origin_server_timestamp: timestamp,
            mxc_url: encode_media_source(&content.source)?,
            original_filename: content
                .filename
                .clone()
                .or_else(|| Some(content.body.clone())),
            mime_type: content.info.as_ref().and_then(|info| info.mimetype.clone()),
            category: MediaCategory::Images,
        }),
        MessageType::Video(content) => Some(AttachmentDiscovery {
            room_id: room_id.to_owned(),
            event_id: event_id.to_owned(),
            origin_server_timestamp: timestamp,
            mxc_url: encode_media_source(&content.source)?,
            original_filename: content
                .filename
                .clone()
                .or_else(|| Some(content.body.clone())),
            mime_type: content.info.as_ref().and_then(|info| info.mimetype.clone()),
            category: MediaCategory::Videos,
        }),
        MessageType::Audio(content) => Some(AttachmentDiscovery {
            room_id: room_id.to_owned(),
            event_id: event_id.to_owned(),
            origin_server_timestamp: timestamp,
            mxc_url: encode_media_source(&content.source)?,
            original_filename: content
                .filename
                .clone()
                .or_else(|| Some(content.body.clone())),
            mime_type: content.info.as_ref().and_then(|info| info.mimetype.clone()),
            category: MediaCategory::Audio,
        }),
        MessageType::File(content) => Some(AttachmentDiscovery {
            room_id: room_id.to_owned(),
            event_id: event_id.to_owned(),
            origin_server_timestamp: timestamp,
            mxc_url: encode_media_source(&content.source)?,
            original_filename: content
                .filename
                .clone()
                .or_else(|| Some(content.body.clone())),
            mime_type: content.info.as_ref().and_then(|info| info.mimetype.clone()),
            category: media_classification::category(
                content.filename.as_deref().or(Some(content.body.as_str())),
                content
                    .info
                    .as_ref()
                    .and_then(|info| info.mimetype.as_deref()),
            ),
        }),
        _ => None,
    };

    Ok(discovery)
}

fn encode_media_source(source: &MediaSource) -> Result<String> {
    match source {
        MediaSource::Plain(uri) => Ok(uri.to_string()),
        MediaSource::Encrypted(file) => Ok(format!("encrypted:{}", serde_json::to_string(file)?)),
    }
}

fn decode_media_source(value: &str) -> Result<MediaSource> {
    if let Some(rest) = value.strip_prefix("encrypted:") {
        let file = serde_json::from_str(rest)?;
        return Ok(MediaSource::Encrypted(file));
    }
    Ok(MediaSource::Plain(OwnedMxcUri::from(value.to_owned())))
}

async fn validate_downloaded_media(path: &Path, category: MediaCategory) -> Result<()> {
    let metadata = tokio_fs::metadata(path).await?;
    if metadata.len() == 0 {
        return Err(anyhow!("file is empty"));
    }

    if category == MediaCategory::Images {
        let bytes = tokio_fs::read(path).await?;
        image::load_from_memory(&bytes).context("Downloaded image could not be decoded")?;
        return Ok(());
    }

    if category == MediaCategory::Videos {
        if probe_with_external_tool("ffprobe", path).await?
            || probe_with_external_tool("ffmpeg", path).await?
        {
            return Ok(());
        }
    }

    Ok(())
}

async fn probe_with_external_tool(tool: &str, path: &Path) -> Result<bool> {
    let executable = which(tool).await?;
    let Some(executable) = executable else {
        return Ok(false);
    };

    let output = if tool == "ffprobe" {
        tokio::process::Command::new(executable)
            .args([
                "-v",
                "error",
                "-show_entries",
                "stream=codec_type,width,height",
                "-of",
                "json",
            ])
            .arg(path)
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .output()
            .await?
    } else {
        tokio::process::Command::new(executable)
            .args(["-v", "error", "-i"])
            .arg(path)
            .args(["-frames:v", "1", "-f", "null", "-"])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .output()
            .await?
    };

    Ok(output.status.success())
}

async fn which(name: &str) -> Result<Option<PathBuf>> {
    let Some(path_var) = std::env::var_os("PATH") else {
        return Ok(None);
    };
    for path in std::env::split_paths(&path_var) {
        if !path.is_absolute() {
            continue;
        }

        for executable in candidate_executable_paths(&path, name) {
            let metadata = match tokio_fs::metadata(&executable).await {
                Ok(metadata) => metadata,
                Err(_) => continue,
            };
            if !metadata.is_file() {
                continue;
            }
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                if metadata.permissions().mode() & 0o111 == 0 {
                    continue;
                }
            }

            let canonical = tokio_fs::canonicalize(&executable)
                .await
                .unwrap_or(executable);
            return Ok(Some(canonical));
        }
    }
    Ok(None)
}

fn candidate_executable_paths(path: &Path, name: &str) -> Vec<PathBuf> {
    #[cfg(windows)]
    {
        return vec![path.join(name), path.join(format!("{name}.exe"))];
    }
    #[cfg(not(windows))]
    {
        vec![path.join(name)]
    }
}

async fn sha256_file(path: &Path) -> Result<String> {
    let bytes = tokio_fs::read(path).await?;
    let mut hasher = Sha256::new();
    hasher.update(bytes);
    Ok(format!("{:x}", hasher.finalize()))
}

async fn move_file_cross_filesystem(from: &Path, to: &Path) -> Result<()> {
    if let Some(parent) = to.parent() {
        tokio_fs::create_dir_all(parent).await?;
    }

    if tokio_fs::symlink_metadata(to)
        .await
        .map(|metadata| metadata.file_type().is_symlink())
        .unwrap_or(false)
    {
        return Err(anyhow!(
            "Refusing to write downloaded media through symbolic link {}",
            to.display()
        ));
    }

    match tokio_fs::hard_link(from, to).await {
        Ok(()) => {
            tokio_fs::remove_file(from).await?;
            Ok(())
        }
        Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => {
            Err(anyhow!("Destination file already exists: {}", to.display()))
        }
        Err(_) => {
            let mut source = TokioFile::open(from).await?;
            let mut destination = tokio_fs::OpenOptions::new()
                .write(true)
                .create_new(true)
                .open(to)
                .await?;
            tokio::io::copy(&mut source, &mut destination).await?;
            destination.flush().await?;
            drop(destination);
            tokio_fs::remove_file(from).await?;
            Ok(())
        }
    }
}

fn relative_storage_path(root: &str, path: &Path) -> String {
    path.strip_prefix(root)
        .unwrap_or(path)
        .to_string_lossy()
        .replace('\\', "/")
        .trim_start_matches('/')
        .to_owned()
}

fn temp_download_path(root: &Path, extension: Option<&str>) -> PathBuf {
    let name = match extension.filter(|extension| !extension.is_empty()) {
        Some(extension) => format!("{}.{}", Uuid::new_v4(), extension),
        None => Uuid::new_v4().to_string(),
    };
    root.join(name)
}

fn parse_mxc_url(value: &str) -> Option<(String, String)> {
    let trimmed = value.strip_prefix("mxc://")?;
    let (server_name, media_id) = trimmed.split_once('/')?;
    if server_name.is_empty() || media_id.is_empty() {
        return None;
    }
    Some((server_name.to_owned(), media_id.to_owned()))
}

fn is_remote_media(mxc_url: &str, homeserver_url: &str) -> bool {
    let Some((server_name, _)) = parse_mxc_url(mxc_url) else {
        return false;
    };
    let remote_host = server_name
        .split(':')
        .next()
        .unwrap_or_default()
        .to_ascii_lowercase();
    let homeserver_host = reqwest::Url::parse(homeserver_url)
        .ok()
        .and_then(|url| url.host_str().map(|host| host.to_ascii_lowercase()))
        .unwrap_or_default();
    !remote_host.is_empty() && remote_host != homeserver_host
}

fn same_origin(left: &reqwest::Url, right: &reqwest::Url) -> bool {
    left.scheme() == right.scheme()
        && left.host_str() == right.host_str()
        && left.port_or_known_default() == right.port_or_known_default()
}

fn should_stop_initial_backfill(checkpoint: &RoomCheckpoint, settings: &AppSettings) -> bool {
    if settings.message_limit > 0 && checkpoint.historical_message_count >= settings.message_limit {
        return true;
    }
    let Some(cutoff) = history_cutoff(settings) else {
        return false;
    };
    checkpoint
        .oldest_backfilled_timestamp
        .is_some_and(|timestamp| timestamp <= cutoff)
}

fn backfill_detail(checkpoint: &RoomCheckpoint, settings: &AppSettings) -> String {
    if settings.message_limit > 0 {
        format!(
            "Scanning {} / {} messages",
            checkpoint.historical_message_count, settings.message_limit
        )
    } else {
        format!("Scanning {} messages", checkpoint.historical_message_count)
    }
}

fn history_cutoff(settings: &AppSettings) -> Option<DateTime<Utc>> {
    if settings.time_window_value <= 0 {
        return None;
    }
    let now = Utc::now();
    match settings.time_window_unit {
        crate::domain::TimeWindowUnit::None => None,
        crate::domain::TimeWindowUnit::Day => {
            Some(now - chrono::Duration::days(i64::from(settings.time_window_value)))
        }
        crate::domain::TimeWindowUnit::Week => {
            Some(now - chrono::Duration::weeks(i64::from(settings.time_window_value)))
        }
        crate::domain::TimeWindowUnit::Month => {
            Some(now - chrono::Duration::days(i64::from(settings.time_window_value) * 30))
        }
    }
}

fn failed_job_cutoff(settings: &AppSettings) -> Option<DateTime<Utc>> {
    if settings.failed_job_retention_value <= 0 {
        return None;
    }
    let value = i64::from(settings.failed_job_retention_value);
    let now = Utc::now();
    match settings.failed_job_retention_unit {
        FailedJobRetentionUnit::None => None,
        FailedJobRetentionUnit::Minute => Some(now - chrono::Duration::minutes(value)),
        FailedJobRetentionUnit::Hour => Some(now - chrono::Duration::hours(value)),
        FailedJobRetentionUnit::Day => Some(now - chrono::Duration::days(value)),
    }
}

async fn cleanup_temp_files(path: &Path) -> Result<()> {
    if !path.exists() {
        return Ok(());
    }
    let mut entries = tokio_fs::read_dir(path).await?;
    while let Some(entry) = entries.next_entry().await? {
        let _ = tokio_fs::remove_file(entry.path()).await;
    }
    Ok(())
}

#[derive(Clone, Copy, Eq, PartialEq)]
enum TimelineSource {
    Live,
    InitialBackfill,
    ReconnectCatchUp,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "snake_case")]
struct SpaceHierarchyResponse {
    rooms: Vec<SpaceHierarchyRoom>,
    #[serde(alias = "nextBatch")]
    next_batch: Option<String>,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "snake_case")]
struct SpaceHierarchyRoom {
    #[serde(alias = "roomId")]
    room_id: Option<String>,
    #[serde(alias = "roomType")]
    room_type: Option<String>,
    name: Option<String>,
    #[serde(alias = "canonicalAlias")]
    canonical_alias: Option<String>,
    #[serde(default, alias = "childrenState")]
    children_state: Vec<SpaceHierarchyChildEvent>,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "snake_case")]
struct SpaceHierarchyChildEvent {
    #[serde(rename = "type")]
    event_type: Option<String>,
    #[serde(alias = "stateKey")]
    state_key: Option<String>,
    #[serde(default)]
    content: SpaceHierarchyChildContent,
}

#[derive(Default, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
struct SpaceHierarchyChildContent {
    via: Option<Vec<String>>,
}
