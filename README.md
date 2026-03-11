# MatrixMediaArchiver Qt

MatrixMediaArchiver Qt is the Qt and Rust desktop rebuild of the Matrix downloader bot for Linux, Windows, and macOS.

## License

This repository is released under the [Matrix Media Archiver Attribution and Non-Commercial License 1.0](LICENSE).

Short version:
- you can use, study, and modify the code
- you can share modified versions
- you must credit Brandon Stone for any code you use from this project
- you cannot sell this software or derivatives, or use them commercially without written permission

This is source-available, not OSI open source, because commercial use is restricted.

## What It Does

- connects to a Matrix account or bot account
- joins rooms and spaces
- scans message history and watches live traffic for media
- builds and processes a download queue
- stores app state in SQLite
- runs as a desktop app on Linux, Windows, and macOS

## Basic Use

1. Open the app.
2. Go to `Settings`.
3. Enter your homeserver URL, username, password, owner Matrix ID, and destination folder.
4. Save settings.
5. Join a room or space by room ID or alias.
6. Turn the power toggle on from the dashboard.
7. Watch `Queue`, `Workers`, and the dashboard log while it scans and downloads.

Chat command currently implemented:

```text
!matrixdl join <room-id-or-alias>
```

Example:

```text
!matrixdl join #goofball:example.org
```

## Running Packaged Builds

Linux x86_64 AppImage:

```bash
chmod +x MatrixMediaArchiverQt-*-linux-x86_64.AppImage
./MatrixMediaArchiverQt-*-linux-x86_64.AppImage
```

Windows x64 zip:
- unzip the release archive
- run `MatrixMediaArchiverQt.exe`

macOS arm64 zip:
- unzip the release archive
- open `MatrixMediaArchiverQt.app`
- if Gatekeeper quarantines it, remove quarantine on your own machine:

```bash
xattr -dr com.apple.quarantine MatrixMediaArchiverQt.app
```

## Repository Layout

- the working cross-platform app lives at the repository root
- the original Swift macOS app is kept locally under `reference/swift-mac-app/` as a behavior reference and is ignored by Git
- local scratch builds and toolchains stay under `.work/`
- packaged binaries are written to `builds/`

## Versioning

- current app version: `2026.3.11.1`
- source of truth: `VERSION.txt`
- format: `year.month.day.build`

The version is shown inside the app Settings page and is used for packaged archive names.

## Building From Source

macOS:

```bash
brew install cmake ninja pkgconf qt rustup
cmake -S . -B .work/macos-dev -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build .work/macos-dev
ctest --test-dir .work/macos-dev --output-on-failure
```

Linux packaging:

```bash
./scripts/package-appimage.sh
```

Windows cross-package from macOS/Linux host:

```bash
./scripts/package-windows-cross.sh
```

Windows native package:

```powershell
./scripts/package-windows.ps1
```

Arch Linux ARM64 helper:

```bash
bash build-linux-arm64.command
```

## GitHub Automation

GitHub Actions is set up in `.github/workflows/desktop-ci.yml`.

- pushes and pull requests build Linux, Windows, and macOS artifacts
- Rust dependencies are cached to avoid repeating the full backend compile every run
- pushes to `main` or `master`, or a manual workflow dispatch, also create or update the latest GitHub release for `v<version>` from `VERSION.txt`

That means once this repository is pushed, GitHub can build the macOS package instead of relying on a local build here.
