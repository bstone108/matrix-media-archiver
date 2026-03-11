use serde::{Deserialize, Serialize};

use crate::domain::{AppSettings, BotRuntimeSnapshot};

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CommandEnvelope {
    pub id: u64,
    pub command: Command,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "camelCase")]
pub enum Command {
    Start {
        settings: AppSettings,
        password: String,
    },
    Stop,
    SaveSettings {
        settings: AppSettings,
        password: String,
    },
    JoinRoom {
        room_id_or_alias: String,
    },
    LeaveRoom {
        room_id: String,
    },
    RequestVerification,
    StartSasVerification,
    ApproveVerification,
    DeclineVerification,
    ResetHistoryScans,
    Shutdown,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "camelCase")]
pub enum ServerEvent {
    Response {
        id: u64,
        ok: bool,
        error: Option<String>,
    },
    Runtime {
        snapshot: BotRuntimeSnapshot,
    },
}
