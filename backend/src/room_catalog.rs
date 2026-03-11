use std::{
    fs,
    path::{Path, PathBuf},
};

use anyhow::{Result, anyhow};
use matrix_sdk::{Room, RoomState};

use crate::{
    database::AppDatabase,
    domain::{AppLogLevel, AppSettings, MediaCategory, RoomRecord},
    folder_naming,
};

#[derive(Clone)]
pub struct RoomCatalog {
    database: AppDatabase,
}

impl RoomCatalog {
    pub fn new(database: AppDatabase) -> Self {
        Self { database }
    }

    pub async fn sync_sdk_room(&self, room: &Room, settings: &AppSettings) -> Result<RoomRecord> {
        let display_name = room
            .display_name()
            .await
            .ok()
            .map(|value| value.to_string())
            .filter(|value| !value.trim().is_empty())
            .or_else(|| room.name().filter(|value| !value.trim().is_empty()));
        let canonical_alias = room.canonical_alias().map(|value| value.to_string());
        let aliases = room
            .alt_aliases()
            .into_iter()
            .map(|value| value.to_string())
            .collect::<Vec<_>>();

        self.sync_resolved_metadata(
            room.room_id().to_string(),
            display_name,
            canonical_alias,
            aliases,
            room.is_space(),
            membership_storage_key(room.state()).to_owned(),
            settings,
        )
        .await
    }

    pub async fn sync_hierarchy_metadata(
        &self,
        room_id: &str,
        display_name: Option<String>,
        canonical_alias: Option<String>,
        membership: &str,
        settings: &AppSettings,
    ) -> Result<RoomRecord> {
        self.sync_resolved_metadata(
            room_id.to_owned(),
            display_name,
            canonical_alias.clone(),
            canonical_alias.into_iter().collect(),
            true,
            membership.to_owned(),
            settings,
        )
        .await
    }

    pub async fn ensure_room_folder(
        &self,
        room_id: &str,
        settings: &AppSettings,
    ) -> Result<PathBuf> {
        if let Some(record) = self.database.room_record(room_id).await? {
            if record.is_space {
                return Err(anyhow!("Spaces do not have download folders: {room_id}"));
            }

            let root = PathBuf::from(&settings.destination_root_path);
            fs::create_dir_all(&root)?;
            let folder = root.join(&record.active_folder_label);
            fs::create_dir_all(&folder)?;
            return Ok(folder);
        }

        let label = folder_naming::sanitize_label(room_id);
        self.database
            .upsert_room(room_id, None, None, &label, false, "joined")
            .await?;

        let folder = PathBuf::from(&settings.destination_root_path).join(label);
        fs::create_dir_all(&folder)?;
        Ok(folder)
    }

    pub async fn category_folder(
        &self,
        room_id: &str,
        category: MediaCategory,
        settings: &AppSettings,
    ) -> Result<PathBuf> {
        let room_folder = self.ensure_room_folder(room_id, settings).await?;
        let category_folder = room_folder.join(category.as_storage_key());
        fs::create_dir_all(&category_folder)?;
        Ok(category_folder)
    }

    async fn sync_resolved_metadata(
        &self,
        room_id: String,
        display_name: Option<String>,
        canonical_alias: Option<String>,
        aliases: Vec<String>,
        is_space: bool,
        membership: String,
        settings: &AppSettings,
    ) -> Result<RoomRecord> {
        let existing = self.database.room_record(&room_id).await?;
        let resolved_display_name = display_name.or_else(|| {
            existing
                .as_ref()
                .and_then(|value| value.current_display_name.clone())
        });
        let resolved_canonical_alias = canonical_alias.or_else(|| {
            existing
                .as_ref()
                .and_then(|value| value.current_canonical_alias.clone())
        });
        let resolved_is_space = is_space
            || existing
                .as_ref()
                .map(|value| value.is_space)
                .unwrap_or(false);

        let mut resolved_aliases = Vec::new();
        for alias in aliases
            .into_iter()
            .chain(resolved_canonical_alias.iter().cloned())
        {
            if !alias.is_empty() && !resolved_aliases.contains(&alias) {
                resolved_aliases.push(alias);
            }
        }

        self.database
            .insert_alias_history(&room_id, &resolved_aliases)
            .await?;
        let alias_history = self.database.alias_history(&room_id).await?;
        let existing_labels = self.database.all_folder_labels(Some(&room_id)).await?;

        let label = folder_naming::preferred_label(
            resolved_display_name.as_deref(),
            resolved_canonical_alias.as_deref(),
            &alias_history,
            &room_id,
            &existing_labels,
        );

        if !resolved_is_space {
            let root = PathBuf::from(&settings.destination_root_path);
            fs::create_dir_all(&root)?;
            maybe_rename_room_folder(&root, existing.as_ref(), &label, &self.database).await?;
            fs::create_dir_all(root.join(&label))?;
        }

        self.database
            .upsert_room(
                &room_id,
                resolved_display_name.as_deref(),
                resolved_canonical_alias.as_deref(),
                &label,
                resolved_is_space,
                &membership,
            )
            .await?;

        if let Some(record) = self.database.room_record(&room_id).await? {
            Ok(record)
        } else {
            Err(anyhow!("Failed to reload room record for {room_id}"))
        }
    }
}

async fn maybe_rename_room_folder(
    root: &Path,
    existing: Option<&RoomRecord>,
    next_label: &str,
    database: &AppDatabase,
) -> Result<()> {
    let Some(existing) = existing else {
        return Ok(());
    };
    if existing.active_folder_label == next_label || existing.is_space {
        return Ok(());
    }

    let old_path = root.join(&existing.active_folder_label);
    let new_path = root.join(next_label);
    if !old_path.exists() || new_path.exists() {
        return Ok(());
    }

    fs::rename(&old_path, &new_path)?;
    database
        .insert_log(
            AppLogLevel::Info,
            "folders",
            &format!(
                "Renamed room folder {} to {}",
                existing.active_folder_label, next_label
            ),
        )
        .await?;
    Ok(())
}

fn membership_storage_key(state: RoomState) -> &'static str {
    match state {
        RoomState::Joined => "joined",
        RoomState::Invited => "invited",
        RoomState::Left => "left",
        RoomState::Knocked => "left",
        RoomState::Banned => "left",
    }
}
