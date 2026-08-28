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
APPLE_SIGNING_IDENTITY="${APPLE_SIGNING_IDENTITY:-${MACOS_CODESIGN_IDENTITY:-}}"
NOTARY_TIMEOUT="${NOTARY_TIMEOUT:-30m}"
# Publish path only. PR/test CI must leave this unset so the app is packaged
# unsigned (no codesign, notarytool, or stapler).
MACOS_REQUIRE_NOTARIZATION="${MACOS_REQUIRE_NOTARIZATION:-0}"

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
  [[ -n "${APPLE_SIGNING_IDENTITY}" && "${APPLE_SIGNING_IDENTITY}" != "-" ]]
}

notarization_requested() {
  [[ -n "${APPLE_ID:-}" && -n "${APPLE_APP_SPECIFIC_PASSWORD:-}" && -n "${APPLE_TEAM_ID:-}" ]]
}

publish_signing_required() {
  [[ "${MACOS_REQUIRE_NOTARIZATION}" == "1" ]]
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

sign_executable() {
  local file="$1"
  echo "Signing executable ${file} ($(file -b "${file}" 2>/dev/null || echo unknown))"
  codesign --force --options runtime --timestamp \
    --entitlements "${ENTITLEMENTS_PATH}" \
    --generate-entitlement-der \
    --sign "${APPLE_SIGNING_IDENTITY}" \
    "${file}"
}

# Sign nested Mach-O files innermost-first, then nested executables (the Rust
# sidecar), then the Qt GUI, then the app bundle. Entitlements apply only to
# executables. The sidecar is a Mach-O executable so the library pass skips it;
# Intel rustc does not ad-hoc-sign, so signing MatrixMediaArchiverQt first
# fails with "code object is not signed at all" in matrix_media_archiver_backend.
sign_app_bundle() {
  local app_bundle="$1"
  local file
  local framework
  local main_exec="${app_bundle}/Contents/MacOS/${APP_NAME}"

  if command -v xattr >/dev/null 2>&1; then
    xattr -cr "${app_bundle}" || true
  fi

  echo "Mach-O classification for Contents/MacOS:"
  shopt -s nullglob
  for file in "${app_bundle}/Contents/MacOS"/*; do
    [[ -f "${file}" ]] || continue
    if is_macho_executable "${file}"; then
      echo "  $(basename "${file}"): $(file -b "${file}" 2>/dev/null || echo unknown) (executable)"
    elif is_macho "${file}"; then
      echo "  $(basename "${file}"): $(file -b "${file}" 2>/dev/null || echo unknown) (mach-o, not executable)"
    else
      echo "  $(basename "${file}"): $(file -b "${file}" 2>/dev/null || echo unknown)"
    fi
  done
  shopt -u nullglob

  while IFS= read -r file; do
    [[ -f "${file}" ]] || continue
    if is_macho "${file}" && ! is_macho_executable "${file}"; then
      echo "Signing library ${file}"
      codesign --force --options runtime --timestamp --sign "${APPLE_SIGNING_IDENTITY}" "${file}"
    fi
  done < <(find "${app_bundle}" -type f | awk '{ print gsub(/\//, "/") "\t" $0 }' | sort -nr | cut -f2-)

  # Sparkle.framework contains Downloader.xpc, Installer.xpc, and Updater.app.
  # Sign those nested bundles before the framework and before the outer app.
  while IFS= read -r xpc; do
    [[ -e "${xpc}" ]] || continue
    echo "Signing XPC service ${xpc}"
    codesign --force --options runtime --timestamp --sign "${APPLE_SIGNING_IDENTITY}" "${xpc}"
  done < <(find "${app_bundle}" -name "*.xpc" | awk '{ print gsub(/\//, "/") "\t" $0 }' | sort -nr | cut -f2-)

  while IFS= read -r nested_app; do
    [[ -d "${nested_app}" ]] || continue
    [[ "${nested_app}" == "${app_bundle}" ]] && continue
    echo "Signing nested app ${nested_app}"
    codesign --force --options runtime --timestamp --sign "${APPLE_SIGNING_IDENTITY}" "${nested_app}"
  done < <(find "${app_bundle}" -name "*.app" -type d | awk '{ print gsub(/\//, "/") "\t" $0 }' | sort -nr | cut -f2-)

  while IFS= read -r framework; do
    [[ -d "${framework}" ]] || continue
    echo "Signing framework ${framework}"
    codesign --force --options runtime --timestamp --sign "${APPLE_SIGNING_IDENTITY}" "${framework}"
  done < <(find "${app_bundle}" -name "*.framework" -type d | awk '{ print gsub(/\//, "/") "\t" $0 }' | sort -nr | cut -f2-)

  while IFS= read -r file; do
    [[ -f "${file}" ]] || continue
    [[ "${file}" == "${main_exec}" ]] && continue
    if is_macho_executable "${file}"; then
      sign_executable "${file}"
    fi
  done < <(find "${app_bundle}" -type f | awk '{ print gsub(/\//, "/") "\t" $0 }' | sort -nr | cut -f2-)

  if [[ ! -f "${main_exec}" ]]; then
    echo "Main executable not found: ${main_exec}" >&2
    exit 1
  fi
  if ! is_macho_executable "${main_exec}"; then
    echo "Main binary is not a Mach-O executable ($(file -b "${main_exec}" 2>/dev/null || echo unknown)): ${main_exec}" >&2
    exit 1
  fi
  sign_executable "${main_exec}"

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
  output_file="$(mktemp "${WORK_DIR}/notary.XXXXXX")"

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

if publish_signing_required; then
  if ! signing_requested; then
    echo "APPLE_SIGNING_IDENTITY must be set for a signed/notarized macOS release." >&2
    exit 1
  fi
  if ! notarization_requested; then
    echo "APPLE_ID, APPLE_APP_SPECIFIC_PASSWORD, and APPLE_TEAM_ID must be set for a signed/notarized macOS release." >&2
    exit 1
  fi
elif [[ -n "${GITHUB_ACTIONS:-}" ]]; then
  # PR/test CI must never codesign or call notarytool, even if secrets are
  # present in the job environment.
  echo "GitHub Actions non-publish run: packaging unsigned (no codesign, notarytool, or stapler)."
  APPLE_SIGNING_IDENTITY=""
  unset APPLE_ID APPLE_APP_SPECIFIC_PASSWORD APPLE_TEAM_ID
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

SPARKLE_FRAMEWORK="${BUILD_DIR}/sparkle/extracted/Sparkle.framework"
if [[ ! -d "${SPARKLE_FRAMEWORK}" ]]; then
  SPARKLE_FRAMEWORK="$(find "${BUILD_DIR}/sparkle" -type d -name Sparkle.framework | head -n 1 || true)"
fi
if [[ -z "${SPARKLE_FRAMEWORK}" || ! -d "${SPARKLE_FRAMEWORK}" ]]; then
  echo "Sparkle.framework missing from the CMake build tree at ${BUILD_DIR}/sparkle" >&2
  exit 1
fi
mkdir -p "${STAGED_APP}/Contents/Frameworks"
rm -rf "${STAGED_APP}/Contents/Frameworks/Sparkle.framework"
ditto "${SPARKLE_FRAMEWORK}" "${STAGED_APP}/Contents/Frameworks/Sparkle.framework"
echo "Embedded Sparkle.framework from ${SPARKLE_FRAMEWORK}"

BACKEND_BIN="${STAGED_APP}/Contents/MacOS/matrix_media_archiver_backend"
if [[ ! -f "${BACKEND_BIN}" ]]; then
  echo "Backend binary missing from app bundle: ${BACKEND_BIN}" >&2
  exit 1
fi

if signing_requested; then
  require_signing_tools
  sign_app_bundle "${STAGED_APP}"
else
  echo "Skipping codesign (unsigned compile/package)."
fi

if notarization_requested; then
  if ! signing_requested; then
    echo "Notarization requires APPLE_SIGNING_IDENTITY." >&2
    exit 1
  fi
  # Zip is the notary vehicle for the .app. Staple the ticket back onto the
  # app bundle, then ship a fresh zip of that stapled .app.
  NOTARY_APP_ZIP="${WORK_DIR}/${APP_NAME}-notarize.zip"
  rm -f "${NOTARY_APP_ZIP}"
  ditto -c -k --sequesterRsrc --keepParent "${STAGED_APP}" "${NOTARY_APP_ZIP}"
  submit_for_notarization "${NOTARY_APP_ZIP}" "app"
  xcrun stapler staple "${STAGED_APP}"
  xcrun stapler validate "${STAGED_APP}"
  rm -f "${NOTARY_APP_ZIP}"
else
  echo "Skipping app notarytool/stapler (unsigned compile/package)."
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
else
  echo "Skipping dmg codesign (unsigned compile/package)."
fi

if notarization_requested; then
  submit_for_notarization "${DMG_PATH}" "dmg"
  xcrun stapler staple "${DMG_PATH}"
  xcrun stapler validate "${DMG_PATH}"
else
  echo "Skipping dmg notarytool/stapler (unsigned compile/package)."
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
