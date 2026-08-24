# Cloud Agent base image for MatrixMediaArchiverQt (Qt 6 + Rust desktop app).
# System packages and stable toolchains live here; per-checkout compilation
# happens in .cursor/install.sh after the repository is checked out.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Build tooling, Qt 6 development files, and the SQLite driver plugin the app
# loads at runtime. Ubuntu 24.04 ships Qt 6.4, which satisfies the project's
# 6.2 minimum. xvfb/xauth let the GUI run headless for smoke tests.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        curl \
        desktop-file-utils \
        git \
        libgl1-mesa-dev \
        libqt6sql6-sqlite \
        libxcb-cursor0 \
        ninja-build \
        patchelf \
        qt6-base-dev \
        qt6-base-dev-tools \
        xauth \
        xvfb \
        zip \
    && rm -rf /var/lib/apt/lists/*

USER ubuntu

# Rust toolchain pinned to backend/rust-toolchain.toml. The backend uses Rust
# edition 2024, which requires a modern toolchain (>= 1.85).
ENV PATH=/home/ubuntu/.cargo/bin:${PATH}
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
      | sh -s -- -y --profile minimal --default-toolchain 1.93.0
