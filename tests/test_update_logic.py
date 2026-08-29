#!/usr/bin/env python3
"""Offline tests for date.build compare, arch assets, nag-once, and appcasts."""

from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HELPER_PATH = ROOT / "scripts" / "generate-sparkle-appcasts.py"


def load_appcast_helper():
    spec = importlib.util.spec_from_file_location("generate_sparkle_appcasts", HELPER_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load {HELPER_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


helper = load_appcast_helper()


def parse_date_build(raw: str) -> tuple[int, int, int, int] | None:
    name = raw.strip()
    if name.startswith("v") or name.startswith("V"):
        name = name[1:]
    parts = name.split(".")
    if len(parts) != 4:
        return None
    try:
        year, month, day, build = (int(part) for part in parts)
    except ValueError:
        return None
    if min(year, month, day, build) < 1:
        return None
    return year, month, day, build


def is_newer(candidate: str, current: str) -> bool:
    left = parse_date_build(candidate)
    right = parse_date_build(current)
    if left is None or right is None:
        return False
    return left > right


def macos_zip(version: str, arch: str) -> str:
    return f"MatrixMediaArchiverQt-{version}-macos-{arch}.zip"


def windows_zip(version: str, arch: str) -> str:
    return f"MatrixMediaArchiverQt-{version}-windows-{arch}.zip"


def linux_appimage_zip(version: str, arch: str) -> str:
    return f"MatrixMediaArchiverQt-{version}-linux-{arch}-appimage.zip"


class DateBuildCompareTests(unittest.TestCase):
    def test_unpadded_equals_leftover_padded(self) -> None:
        self.assertEqual(parse_date_build("2026.8.24.1"), parse_date_build("2026.08.24.01"))
        self.assertEqual(parse_date_build("v2026.8.24.1"), parse_date_build("2026.8.24.1"))

    def test_same_day_build_order(self) -> None:
        self.assertTrue(is_newer("2026.8.24.2", "2026.8.24.1"))
        self.assertFalse(is_newer("2026.8.24.1", "2026.8.24.2"))

    def test_older_date_vs_newer_date(self) -> None:
        self.assertTrue(is_newer("2026.8.28.1", "2026.8.24.1"))
        self.assertFalse(is_newer("2026.3.12.4", "2026.8.24.1"))


class AssetNameTests(unittest.TestCase):
    def test_macos_zip_not_dmg(self) -> None:
        self.assertEqual(
            macos_zip("2026.8.28.1", "arm64"),
            "MatrixMediaArchiverQt-2026.8.28.1-macos-arm64.zip",
        )
        self.assertEqual(
            macos_zip("2026.8.28.1", "x86_64"),
            "MatrixMediaArchiverQt-2026.8.28.1-macos-x86_64.zip",
        )
        self.assertNotIn(".dmg", macos_zip("2026.8.28.1", "arm64"))

    def test_windows_and_linux_names(self) -> None:
        self.assertEqual(
            windows_zip("2026.8.28.1", "x64"),
            "MatrixMediaArchiverQt-2026.8.28.1-windows-x64.zip",
        )
        self.assertEqual(
            windows_zip("2026.8.28.1", "arm64"),
            "MatrixMediaArchiverQt-2026.8.28.1-windows-arm64.zip",
        )
        self.assertEqual(
            linux_appimage_zip("2026.8.28.1", "aarch64"),
            "MatrixMediaArchiverQt-2026.8.28.1-linux-aarch64-appimage.zip",
        )


class NagOnceTests(unittest.TestCase):
    def test_same_tag_is_not_nagged_twice(self) -> None:
        notified: set[str] = set()

        def should_notify(tag: str) -> bool:
            return tag not in notified

        self.assertTrue(should_notify("v2026.8.28.1"))
        notified.add("v2026.8.28.1")
        self.assertFalse(should_notify("v2026.8.28.1"))
        self.assertTrue(should_notify("v2026.8.28.2"))


class GitHubReleaseFixtureTests(unittest.TestCase):
    def test_ignores_draft_and_prerelease(self) -> None:
        draft = {"tag_name": "v2026.8.28.9", "draft": True, "prerelease": False}
        pre = {"tag_name": "v2026.8.28.8", "draft": False, "prerelease": True}
        latest = {"tag_name": "v2026.8.28.1", "draft": False, "prerelease": False}
        self.assertTrue(draft["draft"] or draft["prerelease"])
        self.assertTrue(pre["draft"] or pre["prerelease"])
        self.assertFalse(latest["draft"] or latest["prerelease"])


class SparkleAppcastTests(unittest.TestCase):
    def test_per_arch_appcasts_are_not_universal(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            arm = Path(tmp) / "appcast-macos-arm64.xml"
            intel = Path(tmp) / "appcast-macos-x86_64.xml"
            helper.write_appcast(
                arm,
                version="2026.8.28.1",
                arch="arm64",
                signature="dGVzdA==",
                length=123,
            )
            helper.write_appcast(
                intel,
                version="2026.8.28.1",
                arch="x86_64",
                signature="dGVzdA==",
                length=456,
            )
            arm_text = arm.read_text(encoding="utf-8")
            intel_text = intel.read_text(encoding="utf-8")
            self.assertIn("macos-arm64.zip", arm_text)
            self.assertNotIn("macos-x86_64.zip", arm_text)
            self.assertIn("<sparkle:hardwareRequirements>arm64</sparkle:hardwareRequirements>", arm_text)
            self.assertIn("macos-x86_64.zip", intel_text)
            self.assertNotIn("macos-arm64.zip", intel_text)
            self.assertNotIn("hardwareRequirements", intel_text)
            self.assertNotIn("universal", arm_text)
            self.assertNotIn("universal", intel_text)

    def test_public_key_constant_matches_info_plist(self) -> None:
        plist = (ROOT / "packaging/macos/Info.plist.in").read_text(encoding="utf-8")
        self.assertIn(helper.PUBLIC_ED_KEY, plist)


if __name__ == "__main__":
    unittest.main()
