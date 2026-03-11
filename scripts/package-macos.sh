#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
WORK_DIR="${ROOT_DIR}/.work/macos"
BUILD_DIR="${BUILD_DIR:-${WORK_DIR}/build-release}"
STAGE_DIR="${STAGE_DIR:-${WORK_DIR}/stage-release}"
BUILDS_DIR="${BUILDS_DIR:-${ROOT_DIR}/builds}"
VERSION_FILE="${ROOT_DIR}/VERSION.txt"
APP_NAME="MatrixMediaArchiverQt"
ARCH="$(uname -m)"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This packaging script must be run on macOS." >&2
  exit 1
fi

if [[ ! -f "${VERSION_FILE}" ]]; then
  echo "Version file not found: ${VERSION_FILE}" >&2
  exit 1
fi

APP_VERSION="$(tr -d '\r\n' < "${VERSION_FILE}")"

if [[ -z "${QT_PREFIX:-}" ]]; then
  if command -v qmake6 >/dev/null 2>&1; then
    QT_PREFIX="$(qmake6 -query QT_INSTALL_PREFIX)"
  elif command -v qmake >/dev/null 2>&1; then
    QT_PREFIX="$(qmake -query QT_INSTALL_PREFIX)"
  elif command -v macdeployqt >/dev/null 2>&1; then
    QT_PREFIX="$(cd "$(dirname "$(command -v macdeployqt)")/.." && pwd)"
  else
    QT_PREFIX="/opt/homebrew/opt/qt"
  fi
fi

if [[ -z "${MACDEPLOYQT_BIN:-}" ]]; then
  if command -v macdeployqt >/dev/null 2>&1; then
    MACDEPLOYQT_BIN="$(command -v macdeployqt)"
  else
    MACDEPLOYQT_BIN="${QT_PREFIX}/bin/macdeployqt"
  fi
fi

if [[ ! -x "${MACDEPLOYQT_BIN}" ]]; then
  echo "macdeployqt not found at ${MACDEPLOYQT_BIN}" >&2
  exit 1
fi

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

ARCHIVE_PATH="${BUILDS_DIR}/${APP_NAME}-${APP_VERSION}-macos-${ARCH}.zip"
MATRIX_MEDIA_ARCHIVER_SQLDRIVER_DIR="${QT_PREFIX}/plugins/sqldrivers"
MATRIX_MEDIA_ARCHIVER_SQLDRIVER_STASH_DIR=""

mkdir -p "${WORK_DIR}" "${BUILDS_DIR}"
rm -rf "${BUILD_DIR}" "${STAGE_DIR}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="${QT_PREFIX}" \
  -DMATRIX_MEDIA_ARCHIVER_BUILD_TESTS=OFF
cmake --build "${BUILD_DIR}" --config Release

APP_BUNDLE="${BUILD_DIR}/${APP_NAME}.app"
if [[ ! -d "${APP_BUNDLE}" ]]; then
  echo "Built app bundle not found: ${APP_BUNDLE}" >&2
  exit 1
fi

mkdir -p "${STAGE_DIR}"
cp -R "${APP_BUNDLE}" "${STAGE_DIR}/"

if [[ -d "${MATRIX_MEDIA_ARCHIVER_SQLDRIVER_DIR}" ]]; then
  MATRIX_MEDIA_ARCHIVER_SQLDRIVER_STASH_DIR="$(mktemp -d "${WORK_DIR}/sqldrivers.XXXXXX")"
  trap restore_sql_drivers EXIT
  shopt -s nullglob
  for plugin_path in "${MATRIX_MEDIA_ARCHIVER_SQLDRIVER_DIR}"/libqsql*.dylib; do
    plugin_name="$(basename "${plugin_path}")"
    if [[ "${plugin_name}" != "libqsqlite.dylib" ]]; then
      mv "${plugin_path}" "${MATRIX_MEDIA_ARCHIVER_SQLDRIVER_STASH_DIR}/"
    fi
  done
  shopt -u nullglob
fi

"${MACDEPLOYQT_BIN}" "${STAGE_DIR}/${APP_NAME}.app" -always-overwrite

rm -f "${ARCHIVE_PATH}"
ditto -c -k --sequesterRsrc --keepParent \
  "${STAGE_DIR}/${APP_NAME}.app" \
  "${ARCHIVE_PATH}"

echo "Created ${ARCHIVE_PATH}"
