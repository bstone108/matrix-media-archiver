#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
VERSION_FILE="${ROOT_DIR}/VERSION.txt"

QT_VERSION="${QT_VERSION:-6.10.2}"
QT_WINDOWS_SDK_ROOT="${QT_WINDOWS_SDK_ROOT:-${ROOT_DIR}/.work/qt-sdk/${QT_VERSION}/mingw_64}"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/.work/windows/build}"
STAGE_DIR="${STAGE_DIR:-${ROOT_DIR}/.work/windows/stage}"
BUILDS_DIR="${BUILDS_DIR:-${ROOT_DIR}/builds}"
RUST_TARGET="${RUST_TARGET:-x86_64-pc-windows-gnu}"

if [[ ! -f "${VERSION_FILE}" ]]; then
  echo "Version file not found: ${VERSION_FILE}" >&2
  exit 1
fi

APP_VERSION="$(tr -d '\r\n' < "${VERSION_FILE}")"

if [[ -z "${QT_HOST_PATH:-}" ]]; then
  if [[ "$(uname -s)" == "Darwin" ]]; then
    QT_HOST_PATH="/opt/homebrew/opt/qt"
  else
    QT_HOST_PATH="/usr"
  fi
fi

if [[ -z "${MINGW_PREFIX:-}" ]]; then
  if [[ "$(uname -s)" == "Darwin" ]]; then
    MINGW_PREFIX="/opt/homebrew/bin/x86_64-w64-mingw32"
  else
    MINGW_PREFIX="/usr/bin/x86_64-w64-mingw32"
  fi
fi

MINGW_LINKER="${MINGW_LINKER:-${MINGW_PREFIX}-gcc}"
MINGW_STRIP="${MINGW_STRIP:-${MINGW_PREFIX}-strip}"
QT_TOOLCHAIN_FILE="${QT_TOOLCHAIN_FILE:-${QT_WINDOWS_SDK_ROOT}/lib/cmake/Qt6/qt.toolchain.cmake}"
CHAINLOAD_TOOLCHAIN_FILE="${CHAINLOAD_TOOLCHAIN_FILE:-${ROOT_DIR}/cmake/toolchains/mingw-windows-x86_64.cmake}"
ARCHIVE_PATH="${BUILDS_DIR}/MatrixMediaArchiverQt-${APP_VERSION}-windows-x64.zip"

require_file() {
  local path="$1"
  if [[ ! -f "${path}" ]]; then
    echo "Required file not found: ${path}" >&2
    exit 1
  fi
}

require_dir() {
  local path="$1"
  if [[ ! -d "${path}" ]]; then
    echo "Required directory not found: ${path}" >&2
    exit 1
  fi
}

ensure_cross_build_dir() {
  local cache_file="${BUILD_DIR}/CMakeCache.txt"

  if [[ ! -f "${cache_file}" ]]; then
    return
  fi

  if grep -q 'CMAKE_CXX_COMPILER:FILEPATH=.*/x86_64-w64-mingw32-g++' "${cache_file}" \
    && grep -q 'CMAKE_SYSTEM_NAME:STRING=Windows' "${cache_file}"; then
    return
  fi

  rm -rf "${BUILD_DIR}"
}

create_archive() {
  local archive_path="$1"

  rm -f "${archive_path}"
  if command -v 7z >/dev/null 2>&1; then
    (
      cd "${STAGE_DIR}"
      7z a -bd -mmt="${SEVENZIP_THREADS:-1}" -tzip -mx=9 -mfb=258 -mpass=15 "${archive_path}" .
    )
  else
    (
      cd "${STAGE_DIR}"
      zip -9 -r -X "${archive_path}" .
    )
  fi
}

require_dir "${QT_WINDOWS_SDK_ROOT}"
require_dir "${QT_HOST_PATH}"
require_file "${QT_TOOLCHAIN_FILE}"
require_file "${CHAINLOAD_TOOLCHAIN_FILE}"
require_file "${MINGW_LINKER}"

mkdir -p "${BUILDS_DIR}"
ensure_cross_build_dir
rustup target add "${RUST_TARGET}" >/dev/null

export MATRIX_MEDIA_ARCHIVER_MINGW_PREFIX="${MINGW_PREFIX}"
export CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER="${MINGW_LINKER}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="${QT_TOOLCHAIN_FILE}" \
  -DQT_CHAINLOAD_TOOLCHAIN_FILE="${CHAINLOAD_TOOLCHAIN_FILE}" \
  -DQT_HOST_PATH="${QT_HOST_PATH}" \
  -DMATRIX_MEDIA_ARCHIVER_BACKEND_RUST_TARGET="${RUST_TARGET}" \
  -DMATRIX_MEDIA_ARCHIVER_BUILD_TESTS=OFF

cmake --build "${BUILD_DIR}" --config Release

APP_EXE="${BUILD_DIR}/MatrixMediaArchiverQt.exe"
BACKEND_EXE="${BUILD_DIR}/matrix_media_archiver_backend.exe"
require_file "${APP_EXE}"
require_file "${BACKEND_EXE}"

rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}/platforms" "${STAGE_DIR}/sqldrivers"

cp "${APP_EXE}" "${STAGE_DIR}/"
cp "${BACKEND_EXE}" "${STAGE_DIR}/"

if [[ -x "${MINGW_STRIP}" ]]; then
  "${MINGW_STRIP}" "${STAGE_DIR}/MatrixMediaArchiverQt.exe" || true
  "${MINGW_STRIP}" "${STAGE_DIR}/matrix_media_archiver_backend.exe" || true
fi

for dll in \
  Qt6Core.dll \
  Qt6Gui.dll \
  Qt6Widgets.dll \
  Qt6Sql.dll \
  libgcc_s_seh-1.dll \
  libstdc++-6.dll \
  libwinpthread-1.dll \
  d3dcompiler_47.dll \
  opengl32sw.dll
do
  require_file "${QT_WINDOWS_SDK_ROOT}/bin/${dll}"
  cp "${QT_WINDOWS_SDK_ROOT}/bin/${dll}" "${STAGE_DIR}/"
done

require_file "${QT_WINDOWS_SDK_ROOT}/plugins/platforms/qwindows.dll"
require_file "${QT_WINDOWS_SDK_ROOT}/plugins/sqldrivers/qsqlite.dll"
cp "${QT_WINDOWS_SDK_ROOT}/plugins/platforms/qwindows.dll" "${STAGE_DIR}/platforms/"
cp "${QT_WINDOWS_SDK_ROOT}/plugins/sqldrivers/qsqlite.dll" "${STAGE_DIR}/sqldrivers/"

create_archive "${ARCHIVE_PATH}"

echo "Created ${ARCHIVE_PATH}"
