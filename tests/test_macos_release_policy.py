#!/usr/bin/env python3
"""Guards for unsigned PR macOS CI vs signed publish releases."""

from __future__ import annotations

import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORKFLOW = (ROOT / ".github/workflows/desktop-ci.yml").read_text(encoding="utf-8")
PACKAGE_MACOS = (ROOT / "scripts/package-macos.sh").read_text(encoding="utf-8")


class MacosReleasePolicyTests(unittest.TestCase):
    def test_ships_apple_silicon_and_intel_on_macos_15_runners(self) -> None:
        self.assertIn("runner: macos-15\n", WORKFLOW)
        self.assertIn("runner: macos-15-intel\n", WORKFLOW)
        self.assertIn("arch: arm64\n", WORKFLOW)
        self.assertIn("arch: x86_64\n", WORKFLOW)
        self.assertNotIn("macos-14", WORKFLOW)
        self.assertNotIn("macos-latest", WORKFLOW)

    def test_pull_requests_package_unsigned_without_cert_import(self) -> None:
        self.assertIn("Package unsigned macOS app", WORKFLOW)
        self.assertIn("if: needs.assign-version.outputs.should_stamp != 'true'", WORKFLOW)
        self.assertIn("Import Apple Developer ID certificate", WORKFLOW)
        self.assertIn(
            "if: needs.assign-version.outputs.should_stamp == 'true'",
            WORKFLOW,
        )
        unsigned_idx = WORKFLOW.index("Package unsigned macOS app")
        import_idx = WORKFLOW.index("Import Apple Developer ID certificate")
        notarize_idx = WORKFLOW.index("Package, sign, and notarize macOS app")
        self.assertLess(unsigned_idx, import_idx)
        self.assertLess(import_idx, notarize_idx)
        self.assertIn("MACOS_REQUIRE_NOTARIZATION: \"1\"", WORKFLOW)

    def test_publish_downloads_both_mac_architectures(self) -> None:
        self.assertIn("macos-arm64", WORKFLOW)
        self.assertIn("macos-x86_64", WORKFLOW)
        self.assertIn("Download macOS ARM64 artifacts", WORKFLOW)
        self.assertIn("Download macOS Intel artifacts", WORKFLOW)

    def test_package_script_allows_unsigned_github_actions(self) -> None:
        self.assertNotIn(
            "APPLE_SIGNING_IDENTITY must be set in GitHub Actions.",
            PACKAGE_MACOS,
        )
        self.assertIn("MACOS_REQUIRE_NOTARIZATION", PACKAGE_MACOS)
        self.assertIn("packaging unsigned", PACKAGE_MACOS)
        self.assertIn("Skipping codesign", PACKAGE_MACOS)
        self.assertIn("Skipping app notarytool/stapler", PACKAGE_MACOS)
        self.assertIn("Skipping dmg notarytool/stapler", PACKAGE_MACOS)

    def test_publish_still_uses_zip_as_app_notary_vehicle(self) -> None:
        self.assertIn("ditto -c -k --sequesterRsrc --keepParent", PACKAGE_MACOS)
        self.assertIn("submit_for_notarization \"${NOTARY_APP_ZIP}\" \"app\"", PACKAGE_MACOS)
        self.assertIn("stapler staple \"${STAGED_APP}\"", PACKAGE_MACOS)
        self.assertIn("submit_for_notarization \"${DMG_PATH}\" \"dmg\"", PACKAGE_MACOS)

    def test_signs_nested_macos_executables_before_gui_then_app(self) -> None:
        self.assertIn(
            'main_exec="${app_bundle}/Contents/MacOS/${APP_NAME}"',
            PACKAGE_MACOS,
        )
        skip_gui = PACKAGE_MACOS.index('[[ "${file}" == "${main_exec}" ]] && continue')
        sign_gui = PACKAGE_MACOS.index('sign_executable "${main_exec}"')
        sign_app = PACKAGE_MACOS.index('echo "Signing app bundle ${app_bundle}"')
        self.assertLess(skip_gui, sign_gui)
        self.assertLess(sign_gui, sign_app)
        self.assertIn("matrix_media_archiver_backend", PACKAGE_MACOS)
        self.assertNotIn(
            'find "${app_bundle}/Contents/MacOS" -type f',
            PACKAGE_MACOS,
        )

    def test_embeds_sparkle_framework_after_macdeployqt(self) -> None:
        self.assertIn("Sparkle.framework", PACKAGE_MACOS)
        deploy_idx = PACKAGE_MACOS.index('"${MACDEPLOYQT_BIN}" "${STAGED_APP}" -always-overwrite')
        sparkle_idx = PACKAGE_MACOS.index("Embedded Sparkle.framework from")
        self.assertLess(deploy_idx, sparkle_idx)

    def test_signs_sparkle_xpc_and_nested_apps_before_outer_app(self) -> None:
        xpc_idx = PACKAGE_MACOS.index("Signing XPC service")
        nested_idx = PACKAGE_MACOS.index("Signing nested app")
        framework_idx = PACKAGE_MACOS.index('echo "Signing framework ${framework}"')
        skip_gui = PACKAGE_MACOS.index('[[ "${file}" == "${main_exec}" ]] && continue')
        sign_app = PACKAGE_MACOS.index('echo "Signing app bundle ${app_bundle}"')
        self.assertLess(xpc_idx, framework_idx)
        self.assertLess(nested_idx, skip_gui)
        self.assertLess(framework_idx, sign_app)

    def test_signs_sparkle_autoupdate_before_framework_without_app_entitlements(
        self,
    ) -> None:
        body = _sign_app_bundle_source(PACKAGE_MACOS)
        auto_idx = body.index("Signing Sparkle Autoupdate")
        xpc_idx = body.index("Signing XPC service")
        nested_idx = body.index("Signing nested app")
        framework_idx = body.index('echo "Signing framework ${framework}"')
        remaining_idx = body.index("is_inside_nested_bundle")
        self.assertLess(auto_idx, xpc_idx)
        self.assertLess(xpc_idx, nested_idx)
        self.assertLess(nested_idx, framework_idx)
        self.assertLess(framework_idx, remaining_idx)
        self.assertIn(
            '-path "*/Sparkle.framework/Versions/*/Autoupdate"',
            body,
        )
        auto_block = body[auto_idx:xpc_idx]
        self.assertIn(
            "codesign --force --options runtime --timestamp --sign",
            auto_block,
        )
        self.assertNotIn("--entitlements", auto_block)
        self.assertNotIn("sign_executable", auto_block)
        self.assertNotIn("--deep", auto_block)

    def test_remaining_macho_executables_skip_nested_bundle_paths(self) -> None:
        body = _sign_app_bundle_source(PACKAGE_MACOS)
        skip_gui = body.index('[[ "${file}" == "${main_exec}" ]] && continue')
        remaining_sign = body.index('sign_executable "${file}"')
        sign_gui = body.index('sign_executable "${main_exec}"')
        remaining = body[skip_gui:remaining_sign]
        self.assertLess(skip_gui, remaining_sign)
        self.assertLess(remaining_sign, sign_gui)
        self.assertIn("is_inside_nested_bundle", remaining)
        helper = _bash_function_source(PACKAGE_MACOS, "is_inside_nested_bundle")
        self.assertIn('".framework/"', helper)
        self.assertIn('".xpc/"', helper)
        self.assertIn('".app/"', helper)

    def test_sparkle_nested_helpers_are_not_signed_via_sign_executable(self) -> None:
        body = _sign_app_bundle_source(PACKAGE_MACOS)
        sparkle_start = body.index("Signing Sparkle Autoupdate")
        remaining_start = body.index("is_inside_nested_bundle")
        sparkle_region = body[sparkle_start:remaining_start]
        self.assertNotIn("sign_executable", sparkle_region)
        self.assertNotIn("--entitlements", sparkle_region)
        for helper_echo in (
            "Signing Sparkle Autoupdate",
            "Signing XPC service",
            "Signing nested app",
        ):
            self.assertIn(helper_echo, sparkle_region)
        self.assertEqual(body.count("sign_executable"), 2)
        self.assertIn('sign_executable "${file}"', body)
        self.assertIn('sign_executable "${main_exec}"', body)

    def test_does_not_use_codesign_deep_to_sign(self) -> None:
        for line in PACKAGE_MACOS.splitlines():
            stripped = line.strip()
            if stripped.startswith("#") or "codesign" not in stripped:
                continue
            if "codesign --verify" in stripped:
                continue
            self.assertNotIn("--deep", stripped, stripped)

    def test_is_inside_nested_bundle_matches_sparkle_helpers_not_sidecar(
        self,
    ) -> None:
        helper = _bash_function_source(PACKAGE_MACOS, "is_inside_nested_bundle")
        probe = helper + r"""
app="/tmp/MatrixMediaArchiverQt.app"
check() {
  local file="$1"
  local expect="$2"
  local got=1
  if is_inside_nested_bundle "${app}" "${file}"; then
    got=0
  fi
  if [[ "${got}" != "${expect}" ]]; then
    echo "FAIL ${file} got=${got} expect=${expect}" >&2
    exit 1
  fi
}
check "${app}/Contents/Frameworks/Sparkle.framework/Versions/B/Autoupdate" 0
check "${app}/Contents/Frameworks/Sparkle.framework/Versions/B/XPCServices/Installer.xpc/Contents/MacOS/Installer" 0
check "${app}/Contents/Frameworks/Sparkle.framework/Versions/B/XPCServices/Downloader.xpc/Contents/MacOS/Downloader" 0
check "${app}/Contents/Frameworks/Sparkle.framework/Versions/B/Updater.app/Contents/MacOS/Updater" 0
check "${app}/Contents/MacOS/matrix_media_archiver_backend" 1
check "${app}/Contents/MacOS/MatrixMediaArchiverQt" 1
"""
        completed = subprocess.run(
            ["bash", "-c", probe],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_push_includes_v_star_tags_without_path_filters(self) -> None:
        on_section = _top_level_block(WORKFLOW, "on:")
        push_block = _child_block(on_section, "push:")
        pull_request_block = _child_block(on_section, "pull_request:")
        self.assertIn("tags:", push_block)
        self.assertRegex(push_block, r'["\']v\*["\']')
        self.assertNotIn("paths:", push_block)
        self.assertIn("paths:", pull_request_block)
        self.assertIn("branches:", push_block)
        self.assertIn("main", push_block)
        self.assertIn("master", push_block)

    def test_publish_and_stamp_only_on_v_tag_or_workflow_dispatch(self) -> None:
        publish_expr = _env_value(WORKFLOW, "PUBLISH:")
        self.assertIn("workflow_dispatch", publish_expr)
        self.assertIn("startsWith(github.ref, 'refs/tags/v')", publish_expr)
        self.assertNotRegex(
            publish_expr,
            r"heads/main|heads/master|ref_name == 'main'|ref_name == 'master'",
        )

        release_if = _job_if(WORKFLOW, "publish-release:")
        self.assertIn("workflow_dispatch", release_if)
        self.assertIn("startsWith(github.ref, 'refs/tags/v')", release_if)
        self.assertIn("skip_packaging != 'true'", release_if)
        self.assertNotRegex(
            release_if,
            r"heads/main|heads/master|ref_name == 'main'|ref_name == 'master'",
        )

        self.assertNotIn("push to main/master or", WORKFLOW)
        self.assertNotIn("push to main/master)", WORKFLOW)

    def test_tag_push_reuses_tag_version_and_skips_existing_release(self) -> None:
        self.assertIn("${REF_NAME#v}", WORKFLOW)
        self.assertIn("already has a GitHub release", WORKFLOW)
        self.assertIn("skip_packaging", WORKFLOW)
        self.assertIn(
            "skip_packaging: ${{ steps.version.outputs.skip_packaging }}",
            WORKFLOW,
        )
        self.assertGreaterEqual(
            WORKFLOW.count(
                "needs.assign-version.outputs.skip_packaging != 'true'"
            ),
            5,
        )
        self.assertIn("next-app-version.py --strict", WORKFLOW)
        assign_script = _assign_version_script(WORKFLOW)
        tag_branch = assign_script.split('elif [[ "${PUBLISH}" == "true" ]]; then', 1)[0]
        self.assertIn("${REF_NAME#v}", tag_branch)
        self.assertNotIn("next-app-version.py", tag_branch)


def _sign_app_bundle_source(script: str) -> str:
    return _bash_function_source(script, "sign_app_bundle")


def _bash_function_source(script: str, name: str) -> str:
    lines = script.splitlines(keepends=True)
    start = None
    prefix = f"{name}() {{"
    for index, line in enumerate(lines):
        if line.startswith(prefix):
            start = index
            break
    if start is None:
        raise AssertionError(f"missing function {name}")
    collected = [lines[start]]
    for line in lines[start + 1 :]:
        collected.append(line)
        if line.rstrip("\n") == "}":
            return "".join(collected)
    raise AssertionError(f"unclosed function {name}")


def _top_level_block(text: str, header: str) -> str:
    lines = text.splitlines()
    start = None
    for index, line in enumerate(lines):
        if line == header:
            start = index
            continue
        if (
            start is not None
            and line
            and not line.startswith(" ")
            and not line.startswith("#")
        ):
            return "\n".join(lines[start:index])
    if start is None:
        raise AssertionError(f"missing top-level block {header!r}")
    return "\n".join(lines[start:])


def _child_block(parent: str, header: str) -> str:
    lines = parent.splitlines()
    start = None
    child_indent = None
    for index, line in enumerate(lines):
        stripped = line.lstrip(" ")
        indent = len(line) - len(stripped)
        if stripped.startswith(header) and (
            child_indent is None or indent == child_indent
        ):
            start = index
            child_indent = indent
            continue
        if start is not None and stripped and indent <= child_indent:
            return "\n".join(lines[start:index])
    if start is None:
        raise AssertionError(f"missing child block {header!r}")
    return "\n".join(lines[start:])


def _env_value(text: str, key: str) -> str:
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith(key):
            return stripped.split(":", 1)[1].strip()
    raise AssertionError(f"missing env key {key!r}")


def _job_if(text: str, job_id: str) -> str:
    block = _child_block(_top_level_block(text, "jobs:"), job_id)
    for line in block.splitlines()[1:]:
        stripped = line.strip()
        if stripped.startswith("if:"):
            return stripped.split(":", 1)[1].strip()
        if stripped.startswith("needs:") or stripped.startswith("name:"):
            continue
        if stripped and not stripped.startswith("#"):
            break
    raise AssertionError(f"missing if: on job {job_id!r}")


def _assign_version_script(text: str) -> str:
    marker = "id: version"
    start = text.index(marker)
    run_marker = "run: |"
    run_at = text.index(run_marker, start)
    lines = text[run_at:].splitlines()[1:]
    script_lines = []
    for line in lines:
        if line and not line.startswith(" ") and not line.startswith("\t"):
            break
        if line.startswith("  ") and not line.startswith("    ") and line.strip():
            if script_lines:
                break
        script_lines.append(line)
    return "\n".join(script_lines)


if __name__ == "__main__":
    unittest.main()
