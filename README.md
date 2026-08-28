# MatrixMediaArchiver Qt

MatrixMediaArchiver Qt is a desktop Matrix media downloader for Linux, Windows, and macOS.
Full disclosure, I do utilize AI to assist with coding when I get stuck or for tedius tasks. 
Most is done by me however. Please report issues you find.  Thanks.

## What It Does

- signs into a Matrix account or bot account
- joins rooms and spaces
- scans message history and watches live traffic for media
- builds and processes a download queue
- stores app state locally in SQLite

## Install And Launch

Linux x86_64 and arm64 AppImage:

```bash
chmod +x MatrixMediaArchiverQt-*.AppImage
./MatrixMediaArchiverQt-*.AppImage
```

Windows x64 and arm64 zip:
- unzip the release archive
- open the extracted folder
- run `MatrixMediaArchiverQt.exe`

macOS Apple Silicon (arm64) and Intel (x86_64):

- open the `.dmg`, then drag `MatrixMediaArchiverQt` into `Applications`
- or unzip the `.zip` and open `MatrixMediaArchiverQt.app`

Release builds are Developer ID signed, notarized, and stapled (`.app`, `.dmg`, and `.zip`) on a `v*` tag or `workflow_dispatch`. Pull-request and push-to-main CI compile unsigned.

## First-Time Setup

1. Open the app.
2. Go to `Settings`.
3. Enter your homeserver URL, username, password, owner Matrix ID, and destination folder.
4. Save settings.
5. If you want to limit scanning, set `Message Limit` and `Time Window`.
6. If you want more parallel downloads, increase `Download Workers`.
7. Go back to `Dashboard` and turn `Power` on.
8. Join one or more rooms or spaces.
9. Watch `Dashboard`, `Workers`, and `Queue` while the app scans and downloads.

## Screen Guide

### Dashboard

The `Dashboard` is the main status view.

- `Power` starts or stops the Matrix connection and downloader.
- `Status` shows whether the app is stopped, starting, running, or in an error state.
- `Queue` shows how many items are waiting to download.
- `Runtime` shows:
  - `Logged In`
  - `Account Mode`
  - `Joined Rooms`
  - `Spaces`
  - `Active Downloads`
- `Log` shows timestamped activity messages from the Matrix connection, room joins, queue handling, settings saves, and download activity.

### Workers

The `Workers` page shows what the background workers are doing.

- `Active Workers` is the total number of room workers currently tracked.
- `Live Watchers` is the number of rooms currently being watched for new media.
- `History Tasks` is the number of rooms currently backfilling older messages.
- The table shows each room, whether its live watcher is `Watching` or `Paused`, and the current history mode and detail text.

### Rooms

Use the `Rooms` page for standard Matrix rooms.

- Enter a room ID like `!room:server` or an alias like `#room:server`, then click `Join`.
- The left list shows joined rooms.
- The detail area shows:
  - room title
  - room ID
  - canonical alias
  - current destination folder label
  - live watcher status
  - history worker status
  - known aliases seen for that room
- `Leave Room` removes the room from active monitoring.

### Spaces

Use the `Spaces` page the same way as `Rooms`, but for Matrix spaces.

- Enter a space ID like `!space:server` or an alias like `#space:server`, then click `Join`.
- The page shows the same kinds of details as the room view:
  - space title
  - room ID
  - canonical alias
  - folder label
  - live watcher status
  - history worker status
  - known aliases
- `Leave Space` removes the space from active monitoring.

### Queue

The `Queue` page shows everything the downloader is trying to process.

- `Items Waiting` shows queued items that still need work.
- `Failed` shows permanently failed items.
- `Active` shows how many downloader slots are busy out of the total worker count.

The page has three sections:

- `Active Downloads`
  - shows one line per downloader slot
  - active workers show filename, room, and byte progress
  - idle workers show `Downloader N: Idle`
- `Waiting`
  - shows queued items, cooldown items, and undecryptable pending items
  - columns: `File`, `Room`, `State`, `Error`
- `Failed`
  - shows permanently failed items
  - columns: `ID`, `File`, `Room`, `Error`, `Updated`
  - `Retry All` moves failed items back into the queue
  - `Clear All` removes the failed-job records

### Help

The `Help` page documents the chat command support.

- current command prefix: `!matrixdl`
- currently implemented command:

```text
!matrixdl join <room-id-or-alias>
```

Example:

```text
!matrixdl join #goofball:example.org
```

- only the `Owner Matrix ID` from `Settings` is allowed to send commands
- the app logs each command and whether it was followed
- in dedicated bot mode, replies can be sent back by DM
- in shared owner account mode, command results stay local to the app log
- plain display-name joins are not supported
- use a room alias like `#room:server` or a room ID like `!room:server`

### Settings

The `Settings` page controls connection, scanning, retry, retention, and storage behavior.

- `App Version` shows the current app version
- `Homeserver URL` is your Matrix homeserver, for example `https://matrix.org`
- `Username` is the Matrix account name the app signs in with
- `Password` is the account password
- `Owner Matrix ID` is the account allowed to send chat commands
- `Destination Root` is the main folder where downloads are written
- `Choose…` opens a folder picker for the destination root
- `Message Limit` controls how many messages are examined during history scans
- `Time Window Value` and `Time Window Unit` limit history scanning by age
  - set the unit to `Disabled` to remove the time-based limit
- `Retry Cooldown Minutes` controls how long the app waits before retrying temporary failures
- `Retry Limit` controls how many times a job is retried before becoming a permanent failure
- `Download Workers` controls how many downloads can run in parallel
- `Auto Clear Failed After` and `Failed Retention Unit` control when permanent failures are removed automatically
  - set the unit to `Disabled` or the value to `0` to keep failed items forever
- `Save Settings` writes the settings and updates the running backend
- `Reset History Scans` clears history-scan progress and discovery tracking so joined rooms can be rescanned from scratch

Important note:
- `Reset History Scans` does not delete files already downloaded to disk
- after a reset, matching files should still be skipped when hashes match

### Verification

The `Verification` page is for Matrix device verification.

- `Status` shows the current verification state
- `Device ID` shows the device currently involved in verification
- `Request Verification` asks for verification
- `Start SAS` begins short-auth-string verification
- `Approve` accepts the presented verification values
- `Reject` declines the verification
- `Emoji Verification` shows the emoji sequence when available
- `Decimals` shows the numeric SAS values when available

## License

This project uses [PolyForm Noncommercial 1.0.0](LICENSE).

Short version:
- you can use, modify, and share it for noncommercial purposes
- you must keep the license and the attribution notices from [NOTICE](NOTICE)
- commercial use requires separate permission
