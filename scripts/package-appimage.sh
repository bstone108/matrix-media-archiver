#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${QT_PORT_BUILD_DIR:-${ROOT_DIR}/.work/linux/build}}"
DIST_DIR="${DIST_DIR:-${QT_PORT_DIST_DIR:-${ROOT_DIR}/.work/linux/dist}}"
APPDIR="${APPDIR:-${DIST_DIR}/AppDir}"
TOOLS_DIR="${TOOLS_DIR:-${QT_PORT_TOOLS_DIR:-${ROOT_DIR}/.work/linux/tools}}"
BUILDS_DIR="${BUILDS_DIR:-${QT_PORT_BUILDS_DIR:-${ROOT_DIR}/builds}}"
VERSION_FILE="${ROOT_DIR}/VERSION.txt"
ARCH="${ARCH:-x86_64}"
APP_NAME="MatrixMediaArchiverQt"
DESKTOP_FILE="${ROOT_DIR}/packaging/linux/MatrixMediaArchiverQt.desktop"
ICON_FILE="${ROOT_DIR}/packaging/icons/matrix-media-archiver.png"

if [[ ! -f "${VERSION_FILE}" ]]; then
  echo "Version file not found: ${VERSION_FILE}" >&2
  exit 1
fi

APP_VERSION="$(tr -d '\r\n' < "${VERSION_FILE}")"

if [[ "${ARCH}" != "x86_64" && "${ARCH}" != "aarch64" ]]; then
  echo "AppImage packaging is only configured for x86_64 and aarch64 right now." >&2
  exit 1
fi

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "This packaging script must be run on Linux." >&2
  exit 1
fi

create_archive() {
  local source_path="$1"
  local archive_path="$2"
  local source_dir
  local source_name

  source_dir="$(dirname "${source_path}")"
  source_name="$(basename "${source_path}")"

  rm -f "${archive_path}"
  if command -v 7z >/dev/null 2>&1; then
    (
      cd "${source_dir}"
      7z a -bd -mmt="${SEVENZIP_THREADS:-1}" -tzip -mx=9 -mfb=258 -mpass=15 "${archive_path}" "${source_name}"
    )
  else
    (
      cd "${source_dir}"
      zip -9 -X "${archive_path}" "${source_name}"
    )
  fi
}

find_generated_appimage() {
  local search_root

  for search_root in "${ROOT_DIR}" "${DIST_DIR}"; do
    if [[ ! -d "${search_root}" ]]; then
      continue
    fi

    find "${search_root}" -maxdepth 2 -type f -name '*.AppImage' \
      ! -name 'linuxdeploy*.AppImage' \
      -printf '%T@ %p\n'
  done | sort -nr | awk 'NR == 1 { print $2 }'
}

restore_sql_drivers() {
  if [[ -z "${MATRIX_MEDIA_ARCHIVER_SQLDRIVER_STASH_DIR:-}" ]]; then
    return
  fi

  shopt -s nullglob
  for plugin_path in "${MATRIX_MEDIA_ARCHIVER_SQLDRIVER_STASH_DIR}"/*; do
    mv "${plugin_path}" "${MATRIX_MEDIA_ARCHIVER_SQLDRIVER_DIR}/"
  done
  shopt -u nullglob
  rmdir "${MATRIX_MEDIA_ARCHIVER_SQLDRIVER_STASH_DIR}" 2>/dev/null || true
}

mkdir -p "${DIST_DIR}" "${TOOLS_DIR}" "${BUILDS_DIR}"

QMAKE_BIN="${QMAKE:-}"
if [[ -z "${QMAKE_BIN}" ]]; then
  if command -v qmake6 >/dev/null 2>&1; then
    QMAKE_BIN="$(command -v qmake6)"
  elif command -v qmake >/dev/null 2>&1; then
    QMAKE_BIN="$(command -v qmake)"
  else
    echo "Could not find qmake/qmake6 on PATH. Install Qt 6 first." >&2
    exit 1
  fi
fi

LINUXDEPLOY_APPIMAGE="${TOOLS_DIR}/linuxdeploy-${ARCH}.AppImage"
LINUXDEPLOY_QT_PLUGIN="${TOOLS_DIR}/linuxdeploy-plugin-qt-${ARCH}.AppImage"

if [[ ! -x "${LINUXDEPLOY_APPIMAGE}" ]]; then
  curl -L \
    -o "${LINUXDEPLOY_APPIMAGE}" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage"
  chmod +x "${LINUXDEPLOY_APPIMAGE}"
fi

if [[ ! -x "${LINUXDEPLOY_QT_PLUGIN}" ]]; then
  curl -L \
    -o "${LINUXDEPLOY_QT_PLUGIN}" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage"
  chmod +x "${LINUXDEPLOY_QT_PLUGIN}"
fi

QT_PREFIX="$("${QMAKE_BIN}" -query QT_INSTALL_PREFIX)"
MATRIX_MEDIA_ARCHIVER_SQLDRIVER_DIR="${QT_PREFIX}/plugins/sqldrivers"
MATRIX_MEDIA_ARCHIVER_SQLDRIVER_STASH_DIR=""

export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE="${QMAKE_BIN}"
export EXTRA_QT_PLUGINS="sqldrivers"
export LINUXDEPLOY_OUTPUT_VERSION="${VERSION:-${APP_VERSION}}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="${QT_PREFIX}"
cmake --build "${BUILD_DIR}" --config Release
ctest --test-dir "${BUILD_DIR}" --build-config Release --output-on-failure

rm -rf "${APPDIR}"
cmake --install "${BUILD_DIR}" --prefix "${APPDIR}/usr"

if [[ -d "${MATRIX_MEDIA_ARCHIVER_SQLDRIVER_DIR}" ]]; then
  MATRIX_MEDIA_ARCHIVER_SQLDRIVER_STASH_DIR="$(mktemp -d "${TOOLS_DIR}/sqldrivers.XXXXXX")"
  trap restore_sql_drivers EXIT
  shopt -s nullglob
  for plugin_path in "${MATRIX_MEDIA_ARCHIVER_SQLDRIVER_DIR}"/libqsql*.so*; do
    plugin_name="$(basename "${plugin_path}")"
    if [[ "${plugin_name}" != libqsqlite.so* ]]; then
      mv "${plugin_path}" "${MATRIX_MEDIA_ARCHIVER_SQLDRIVER_STASH_DIR}/"
    fi
  done
  shopt -u nullglob
fi

"${LINUXDEPLOY_APPIMAGE}" \
  --appdir "${APPDIR}" \
  --executable "${APPDIR}/usr/bin/${APP_NAME}" \
  --desktop-file "${DESKTOP_FILE}" \
  --icon-file "${ICON_FILE}" \
  --plugin qt \
  --output appimage

APPIMAGE_PATH="$(find_generated_appimage)"

if [[ -z "${APPIMAGE_PATH}" ]]; then
  echo "linuxdeploy finished without producing an AppImage." >&2
  exit 1
fi

FINAL_APPIMAGE="${DIST_DIR}/${APP_NAME}-${ARCH}.AppImage"
mv -f "${APPIMAGE_PATH}" "${FINAL_APPIMAGE}"

FINAL_ARCHIVE="${BUILDS_DIR}/${APP_NAME}-${APP_VERSION}-linux-${ARCH}-appimage.zip"
create_archive "${FINAL_APPIMAGE}" "${FINAL_ARCHIVE}"
echo "Created ${FINAL_ARCHIVE}"
