This package is the Qt and Rust source tree for the Matrix downloader port, trimmed for ARM build work.

Included entry point:
- `build-linux-arm64.command`

What the script does:
- runs natively on Arch Linux arm64/aarch64
- installs the needed Arch packages with `pacman`
- sets up Rust with `rustup`
- builds the Qt app plus Rust backend
- packages a Linux `aarch64` AppImage zip into `out/`

Expected output after a successful run:
- `out/MatrixMediaArchiverQt-2026.3.12.3-linux-aarch64-appimage.zip`

What is not automated here:
- Windows ARM64 packaging from this Mac host

Reason:
- the Linux ARM64 path is realistic and self-contained on Arch Linux ARM
- the Windows ARM64 path needs a different Qt SDK and Windows-native toolchain setup that is not reliably scriptable from this repo alone
