#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
WORK_DIR="${ROOT_DIR}/.work/macos"
BUILD_DIR="${BUILD_DIR:-${WORK_DIR}/build-release}"
STAGE_DIR="${STAGE_DIR:-${WORK_DIR}/stage-release}"
BUILDS_DIR="${BUILDS_DIR:-${ROOT_DIR}/builds}"
VERSION_FILE="${ROOT_DIR}/VERSION.txt"
ENTITLEMENTS_PATH="${ROOT_DIR}/packaging/macos/MatrixMediaArchiverQt.entitlements"
APP_NAME="MatrixMediaArchiverQt"
ARCH="$(uname -m)"
APPLE_SIGNING_IDENTITY="${APPLE_SIGNING_IDENTITY:-}"
NOTARY_TIMEOUT="${NOTARY_TIMEOUT:-30m}"

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

signing_requested() {
  [[ -n "${APPLE_SIGNING_IDENTITY}" ]]
}

notarization_requested() {
  [[ -n "${APPLE_ID:-}" && -n "${APPLE_APP_SPECIFIC_PASSWORD:-}" && -n "${APPLE_TEAM_ID:-}" ]]
}

require_signing_tools() {
  if [[ ! -f "${ENTITLEMENTS_PATH}" ]]; then
    echo "Entitlements file not found: ${ENTITLEMENTS_PATH}" >&2
    exit 1
  fi
  if ! command -v codesign >/dev/null 2>&1; then
    echo "codesign is required to sign the macOS app." >&2
    exit 1
  fi
  if ! security find-identity -v -p codesigning | grep -F "${APPLE_SIGNING_IDENTITY}" >/dev/null; then
    echo "Signing identity not found in the keychain: ${APPLE_SIGNING_IDENTITY}" >&2
    security find-identity -v -p codesigning >&2 || true
    exit 1
  fi
}

is_macho() {
  local desc
  desc="$(file -b "$1" 2>/dev/null || true)"
  [[ "${desc}" == Mach-O* ]]
}

is_macho_executable() {
  local desc
  desc="$(file -b "$1" 2>/dev/null || true)"
  [[ "${desc}" == Mach-O*executable* ]]
}

# Sign nested Mach-O files innermost-first, then the app bundle.
# Entitlements are applied only to executables (the Qt GUI and Rust backend).
sign_app_bundle() {
  local app_bundle="$1"
  local file
  local framework

  if command -v xattr >/dev/null 2>&1; then
    xattr -cr "${app_bundle}" || true
  fi

  while IFS= read -r file; do
    [[ -f "${file}" ]] || continue
    if is_macho "${file}" && ! is_macho_executable "${file}"; then
      echo "Signing library ${file}"
      codesign --force --options runtime --timestamp --sign "${APPLE_SIGNING_IDENTITY}" "${file}"
    fi
  done < <(find "${app_bundle}" -type f | awk '{ print gsub(/\//, "/") "\t" $0 }' | sort -nr | cut -f2-)

  while IFS= read -r framework; do
    [[ -d "${framework}" ]] || continue
    echo "Signing framework ${framework}"
    codesign --force --options runtime --timestamp --sign "${APPLE_SIGNING_IDENTITY}" "${framework}"
  done < <(find "${app_bundle}" -name "*.framework" -type d | awk '{ print gsub(/\//, "/") "\t" $0 }' | sort -nr | cut -f2-)

  while IFS= read -r file; do
    [[ -f "${file}" ]] || continue
    if is_macho_executable "${file}"; then
      echo "Signing executable ${file}"
      codesign --force --options runtime --timestamp \
        --entitlements "${ENTITLEMENTS_PATH}" \
        --generate-entitlement-der \
        --sign "${APPLE_SIGNING_IDENTITY}" \
        "${file}"
    fi
  done < <(find "${app_bundle}/Contents/MacOS" -type f)

  echo "Signing app bundle ${app_bundle}"
  codesign --force --options runtime --timestamp \
    --entitlements "${ENTITLEMENTS_PATH}" \
    --generate-entitlement-der \
    --sign "${APPLE_SIGNING_IDENTITY}" \
    "${app_bundle}"

  codesign --verify --deep --strict --verbose=2 "${app_bundle}"
  echo "App code signature:"
  codesign -dvv --entitlements - "${app_bundle}" 2>&1
}

fetch_notary_log() {
  local json_path="$1"
  local submission_id=""

  if command -v python3 >/dev/null 2>&1; then
    submission_id="$(python3 -c 'import json,sys
try:
    print(json.load(open(sys.argv[1])).get("id") or "")
except Exception:
    pass
' "${json_path}" 2>/dev/null || true)"
  fi

  if [[ -z "${submission_id}" ]]; then
    echo "Unable to parse notarization submission id from ${json_path}" >&2
    cat "${json_path}" >&2 || true
    return 1
  fi

  echo "Fetching notarization log for ${submission_id}" >&2
  xcrun notarytool log "${submission_id}" \
    --apple-id "${APPLE_ID}" \
    --password "${APPLE_APP_SPECIFIC_PASSWORD}" \
    --team-id "${APPLE_TEAM_ID}" >&2 || true
}

submit_for_notarization() {
  local artifact_path="$1"
  local label="$2"
  local output_file
  local status=""

  echo "Submitting ${label} for notarization: ${artifact_path}"
  output_file="$(mktemp "${WORK_DIR}/notary.XXXXXX.json")"

  if ! xcrun notarytool submit "${artifact_path}" \
      --apple-id "${APPLE_ID}" \
      --password "${APPLE_APP_SPECIFIC_PASSWORD}" \
      --team-id "${APPLE_TEAM_ID}" \
      --wait \
      --timeout "${NOTARY_TIMEOUT}" \
      --output-format json \
      > "${output_file}"; then
    echo "notarytool submit failed for ${label}" >&2
    cat "${output_file}" >&2 || true
    fetch_notary_log "${output_file}" || true
    exit 1
  fi

  cat "${output_file}"
  if command -v python3 >/dev/null 2>&1; then
    status="$(python3 -c 'import json,sys
print(json.load(open(sys.argv[1])).get("status") or "")
' "${output_file}")"
  fi

  if [[ "${status}" != "Accepted" ]]; then
    echo "Notarization for ${label} finished with status: ${status:-unknown}" >&2
    fetch_notary_log "${output_file}" || true
    exit 1
  fi
}

create_dmg() {
  local app_bundle="$1"
  local dmg_path="$2"
  local dmg_stage="${WORK_DIR}/dmg-root"

  rm -rf "${dmg_stage}"
  mkdir -p "${dmg_stage}"
  ditto "${app_bundle}" "${dmg_stage}/${APP_NAME}.app"
  ln -s /Applications "${dmg_stage}/Applications"

  rm -f "${dmg_path}"
  hdiutil create \
    -volname "${APP_NAME}" \
    -srcfolder "${dmg_stage}" \
    -ov \
    -format UDZO \
    -imagekey zlib-level=9 \
    "${dmg_path}"
}

ARCHIVE_PATH="${BUILDS_DIR}/${APP_NAME}-${APP_VERSION}-macos-${ARCH}.zip"
DMG_PATH="${BUILDS_DIR}/${APP_NAME}-${APP_VERSION}-macos-${ARCH}.dmg"
MATRIX_MEDIA_ARCHIVER_SQLDRIVER_DIR="${QT_PREFIX}/plugins/sqldrivers"
MATRIX_MEDIA_ARCHIVER_SQLDRIVER_STASH_DIR=""

if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
  if ! signing_requested; then
    echo "APPLE_SIGNING_IDENTITY must be set in GitHub Actions." >&2
    exit 1
  fi
  if ! notarization_requested; then
    echo "APPLE_ID, APPLE_APP_SPECIFIC_PASSWORD, and APPLE_TEAM_ID must be set in GitHub Actions." >&2
    exit 1
  fi
fi

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
ditto "${APP_BUNDLE}" "${STAGE_DIR}/${APP_NAME}.app"
STAGED_APP="${STAGE_DIR}/${APP_NAME}.app"

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

"${MACDEPLOYQT_BIN}" "${STAGED_APP}" -always-overwrite

BACKEND_BIN="${STAGED_APP}/Contents/MacOS/matrix_media_archiver_backend"
if [[ ! -f "${BACKEND_BIN}" ]]; then
  echo "Backend binary missing from app bundle: ${BACKEND_BIN}" >&2
  exit 1
fi

if signing_requested; then
  require_signing_tools
  sign_app_bundle "${STAGED_APP}"
fi

if notarization_requested; then
  if ! signing_requested; then
    echo "Notarization requires APPLE_SIGNING_IDENTITY." >&2
    exit 1
  fi
  NOTARY_APP_ZIP="${WORK_DIR}/${APP_NAME}-notarize.zip"
  rm -f "${NOTARY_APP_ZIP}"
  ditto -c -k --sequesterRsrc --keepParent "${STAGED_APP}" "${NOTARY_APP_ZIP}"
  submit_for_notarization "${NOTARY_APP_ZIP}" "app"
  xcrun stapler staple "${STAGED_APP}"
  xcrun stapler validate "${STAGED_APP}"
  rm -f "${NOTARY_APP_ZIP}"
fi

rm -f "${ARCHIVE_PATH}"
ditto -c -k --sequesterRsrc --keepParent \
  "${STAGED_APP}" \
  "${ARCHIVE_PATH}"

create_dmg "${STAGED_APP}" "${DMG_PATH}"

if signing_requested; then
  echo "Signing disk image ${DMG_PATH}"
  codesign --force --sign "${APPLE_SIGNING_IDENTITY}" --timestamp "${DMG_PATH}"
  codesign --verify --verbose=2 "${DMG_PATH}"
fi

if notarization_requested; then
  submit_for_notarization "${DMG_PATH}" "dmg"
  xcrun stapler staple "${DMG_PATH}"
  xcrun stapler validate "${DMG_PATH}"
fi

echo "Created ${ARCHIVE_PATH}"
echo "Created ${DMG_PATH}"

if signing_requested; then
  echo "Signed app identity:"
  codesign -dvv "${STAGED_APP}" 2>&1 | grep -E "Authority|Identifier|TeamIdentifier|Runtime|Signature=" || true
  echo "Signed dmg identity:"
  codesign -dvv "${DMG_PATH}" 2>&1 | grep -E "Authority|Identifier|TeamIdentifier|Signature=" || true
fi

if notarization_requested; then
  echo "Stapler status:"
  xcrun stapler validate "${STAGED_APP}"
  xcrun stapler validate "${DMG_PATH}"
fi
