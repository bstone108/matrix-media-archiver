use std::{
    fs,
    io::Write,
    path::{Path, PathBuf},
};

use anyhow::{Context, Result};
use serde_json::{Map, Value};
use uuid::Uuid;

use crate::domain::StoredSession;

#[derive(Clone, Debug)]
pub struct SecretStore {
    path: std::path::PathBuf,
}

impl SecretStore {
    pub fn new(path: std::path::PathBuf) -> Self {
        Self { path }
    }

    pub fn load_password(&self) -> Result<String> {
        Ok(self
            .read_json()?
            .get("password")
            .and_then(Value::as_str)
            .unwrap_or_default()
            .to_owned())
    }

    pub fn save_password(&self, password: &str) -> Result<()> {
        let mut root = self.read_json()?;
        root.insert("password".to_owned(), Value::String(password.to_owned()));
        self.write_json(&root)
    }

    pub fn load_session(&self) -> Result<Option<StoredSession>> {
        let root = self.read_json()?;
        let Some(value) = root.get("session") else {
            return Ok(None);
        };

        Ok(Some(
            serde_json::from_value(value.clone()).context("Failed to decode stored session")?,
        ))
    }

    pub fn save_session(&self, session: &StoredSession) -> Result<()> {
        let mut root = self.read_json()?;
        root.insert(
            "session".to_owned(),
            serde_json::to_value(session).context("Failed to encode session")?,
        );
        self.write_json(&root)
    }

    pub fn clear_session(&self) -> Result<()> {
        let mut root = self.read_json()?;
        root.remove("session");
        self.write_json(&root)
    }

    fn read_json(&self) -> Result<Map<String, Value>> {
        if !self.path.exists() {
            return Ok(Map::new());
        }
        if is_symlink(&self.path)? {
            return Ok(Map::new());
        }

        let bytes = fs::read(&self.path)
            .with_context(|| format!("Failed to read {}", self.path.display()))?;
        let value: Value = serde_json::from_slice(&bytes)
            .with_context(|| format!("Failed to parse {}", self.path.display()))?;
        Ok(value.as_object().cloned().unwrap_or_default())
    }

    fn write_json(&self, object: &Map<String, Value>) -> Result<()> {
        if let Some(parent) = self.path.parent() {
            fs::create_dir_all(parent)
                .with_context(|| format!("Failed to create {}", parent.display()))?;
        }

        let bytes = serde_json::to_vec_pretty(&Value::Object(object.clone()))
            .context("Failed to serialize secret store")?;
        if is_symlink(&self.path)? {
            fs::remove_file(&self.path)
                .with_context(|| format!("Failed to remove symlink {}", self.path.display()))?;
        }

        let temp_path = temp_secret_store_path(&self.path);
        let mut file = fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&temp_path)
            .with_context(|| format!("Failed to create {}", temp_path.display()))?;
        secure_file(&temp_path)?;
        file.write_all(&bytes)
            .with_context(|| format!("Failed to write {}", temp_path.display()))?;
        file.sync_all()
            .with_context(|| format!("Failed to flush {}", temp_path.display()))?;
        drop(file);

        #[cfg(windows)]
        if self.path.exists() {
            fs::remove_file(&self.path)
                .with_context(|| format!("Failed to replace {}", self.path.display()))?;
        }

        fs::rename(&temp_path, &self.path)
            .with_context(|| format!("Failed to move {} into place", self.path.display()))?;
        secure_file(&self.path)
    }
}

fn is_symlink(path: &Path) -> Result<bool> {
    Ok(fs::symlink_metadata(path)
        .map(|metadata| metadata.file_type().is_symlink())
        .unwrap_or(false))
}

fn temp_secret_store_path(path: &Path) -> PathBuf {
    let file_name = path
        .file_name()
        .map(|name| name.to_string_lossy().into_owned())
        .unwrap_or_else(|| "secret-store".to_owned());
    path.with_file_name(format!(".{file_name}.{}.tmp", Uuid::new_v4()))
}

fn secure_file(path: &Path) -> Result<()> {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;

        let permissions = fs::Permissions::from_mode(0o600);
        fs::set_permissions(path, permissions)
            .with_context(|| format!("Failed to secure {}", path.display()))?;
    }
    Ok(())
}
