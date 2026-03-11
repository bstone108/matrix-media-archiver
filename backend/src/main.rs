mod app_paths;
mod database;
mod domain;
mod folder_naming;
mod media_classification;
mod protocol;
mod room_catalog;
mod secret_store;
mod service;

use std::env;

use anyhow::{Context, Result, anyhow};
use tokio::{
    io::{AsyncBufReadExt, AsyncWriteExt, BufReader},
    sync::mpsc,
};

use crate::{
    app_paths::AppPaths,
    protocol::{CommandEnvelope, ServerEvent},
    service::{BackendService, CommandOutcome},
};

#[tokio::main]
async fn main() -> Result<()> {
    let root_path = parse_root_path()?;
    let paths = AppPaths::from_root(root_path);
    paths.ensure_directories()?;

    let (event_tx, mut event_rx) = mpsc::unbounded_channel::<ServerEvent>();
    let mut service = BackendService::new(paths, event_tx.clone()).await?;

    let writer_task = tokio::spawn(async move {
        let mut stdout = tokio::io::stdout();
        while let Some(event) = event_rx.recv().await {
            if let Ok(mut bytes) = serde_json::to_vec(&event) {
                bytes.push(b'\n');
                if stdout.write_all(&bytes).await.is_err() {
                    break;
                }
                if stdout.flush().await.is_err() {
                    break;
                }
            }
        }
    });

    let stdin = tokio::io::stdin();
    let mut reader = BufReader::new(stdin).lines();
    while let Some(line) = reader.next_line().await? {
        if line.trim().is_empty() {
            continue;
        }

        let envelope: CommandEnvelope = match serde_json::from_str(&line) {
            Ok(envelope) => envelope,
            Err(error) => {
                let _ = event_tx.send(ServerEvent::Response {
                    id: 0,
                    ok: false,
                    error: Some(format!("Invalid command payload: {error}")),
                });
                continue;
            }
        };

        match service.handle_command(envelope.command).await {
            Ok(CommandOutcome::Continue) => {
                let _ = event_tx.send(ServerEvent::Response {
                    id: envelope.id,
                    ok: true,
                    error: None,
                });
            }
            Ok(CommandOutcome::Shutdown) => {
                let _ = event_tx.send(ServerEvent::Response {
                    id: envelope.id,
                    ok: true,
                    error: None,
                });
                break;
            }
            Err(error) => {
                let _ = event_tx.send(ServerEvent::Response {
                    id: envelope.id,
                    ok: false,
                    error: Some(format!("{error:#}")),
                });
            }
        }
    }

    drop(event_tx);
    let _ = writer_task.await;
    Ok(())
}

fn parse_root_path() -> Result<std::path::PathBuf> {
    let mut args = env::args().skip(1);
    while let Some(argument) = args.next() {
        if argument == "--root-path" {
            let value = args.next().context("Missing value for --root-path")?;
            return Ok(std::path::PathBuf::from(value));
        }
    }
    Err(anyhow!("The backend requires --root-path <path>"))
}
