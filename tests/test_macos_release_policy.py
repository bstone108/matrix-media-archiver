#!/usr/bin/env python3
"""Guards for unsigned PR macOS CI vs signed publish releases."""

from __future__ import annotations

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


if __name__ == "__main__":
    unittest.main()
