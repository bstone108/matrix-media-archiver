#!/usr/bin/env bash
# Idempotent per-checkout bootstrap for MatrixMediaArchiverQt.
# Runs from /workspace after the repository is checked out.
set -euo pipefail

# Ensure the pinned Rust toolchain is available (backend/rust-toolchain.toml
# pins 1.93.0). This is a no-op when the image already provides it.
rustup toolchain install 1.93.0 --profile minimal --no-self-update

# Configure and build the Qt app together with the Rust backend sidecar.
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
