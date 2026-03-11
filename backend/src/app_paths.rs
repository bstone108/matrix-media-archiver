use std::{
    fs,
    path::{Path, PathBuf},
};

use anyhow::{Context, Result};

#[derive(Clone, Debug)]
pub struct AppPaths {
    pub root_path: PathBuf,
    pub database_path: PathBuf,
    pub matrix_data_path: PathBuf,
    pub matrix_cache_path: PathBuf,
    pub temp_downloads_path: PathBuf,
    pub secret_store_path: PathBuf,
}

impl AppPaths {
    pub fn from_root(root_path: PathBuf) -> Self {
        Self {
            database_path: root_path.join("app.sqlite"),
            matrix_data_path: root_path.join("matrix-sdk").join("data"),
            matrix_cache_path: root_path.join("matrix-sdk").join("cache"),
            temp_downloads_path: root_path.join("tmp-downloads"),
            secret_store_path: root_path.join("secrets.json"),
            root_path,
        }
    }

    pub fn ensure_directories(&self) -> Result<()> {
        self.ensure_directory(&self.root_path)?;
        self.ensure_directory(&self.matrix_data_path)?;
        self.ensure_directory(&self.matrix_cache_path)?;
        self.ensure_directory(&self.temp_downloads_path)?;
        Ok(())
    }

    fn ensure_directory(&self, path: &Path) -> Result<()> {
        fs::create_dir_all(path)
            .with_context(|| format!("Failed to create directory {}", path.display()))?;
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;

            let permissions = fs::Permissions::from_mode(0o700);
            fs::set_permissions(path, permissions)
                .with_context(|| format!("Failed to secure directory {}", path.display()))?;
        }
        Ok(())
    }
}
