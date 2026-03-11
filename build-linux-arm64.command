#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_ROOT="${SCRIPT_DIR}"
PROJECT_ROOT="${WORK_ROOT}"
OUTPUT_ROOT="${WORK_ROOT}/out"

if [[ ! -f "${PROJECT_ROOT}/CMakeLists.txt" ]]; then
  echo "Could not find the project root next to this script." >&2
  exit 1
fi

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "This helper is intended to be run on Linux." >&2
  exit 1
fi

if [[ ! -f /etc/arch-release ]]; then
  echo "This helper expects Arch Linux." >&2
  exit 1
fi

HOST_ARCH="$(uname -m)"
if [[ "${HOST_ARCH}" != "aarch64" && "${HOST_ARCH}" != "arm64" ]]; then
  echo "This helper expects an arm64/aarch64 Arch Linux host." >&2
  exit 1
fi

PACMAN=(pacman)
if [[ "${EUID}" -ne 0 ]]; then
  PACMAN=(sudo pacman)
fi

mkdir -p "${OUTPUT_ROOT}"
echo "Installing Arch Linux build dependencies..."
"${PACMAN[@]}" -Sy --needed --noconfirm \
  base-devel \
  cmake \
  ninja \
  qt6-base \
  rustup \
  curl \
  patchelf \
  desktop-file-utils \
  zip \
  unzip \
  git \
  xcb-util-cursor \
  7zip

export PATH="${HOME}/.cargo/bin:${PATH}"
rustup default stable

cd "${PROJECT_ROOT}"
ARCH=aarch64 \
BUILD_DIR="${OUTPUT_ROOT}/linux-arm64/build" \
DIST_DIR="${OUTPUT_ROOT}/linux-arm64/dist" \
TOOLS_DIR="${OUTPUT_ROOT}/linux-arm64/tools" \
BUILDS_DIR="${OUTPUT_ROOT}" \
./scripts/package-appimage.sh

echo
echo "Linux arm64 AppImage output should now be under:"
echo "  ${OUTPUT_ROOT}"
