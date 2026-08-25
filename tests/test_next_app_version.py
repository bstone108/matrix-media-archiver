#!/usr/bin/env python3
"""Tests for scripts/next-app-version.py."""

from __future__ import annotations

import importlib.util
import subprocess
import sys
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path
from zoneinfo import ZoneInfo

HELPER_PATH = Path(__file__).resolve().parent.parent / "scripts" / "next-app-version.py"
CHICAGO = ZoneInfo("America/Chicago")


def load_helper():
    spec = importlib.util.spec_from_file_location("next_app_version", HELPER_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load {HELPER_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


helper = load_helper()


class NextAppVersionTests(unittest.TestCase):
    def test_chicago_date_uses_central_time_not_utc(self) -> None:
        # 24 Aug 2026 22:30 CDT is already 25 Aug 2026 03:30 UTC.
        now = datetime(2026, 8, 25, 3, 30, tzinfo=timezone.utc)
        self.assertEqual(helper.chicago_date_components(now), (2026, 8, 24))

    def test_does_not_pick_utc_tomorrow_while_evening_in_chicago(self) -> None:
        now = datetime(2026, 8, 25, 3, 30, tzinfo=timezone.utc)
        version = helper.next_version([], now=now)
        self.assertEqual(version, "2026.8.24.1")
        self.assertFalse(version.startswith("2026.8.25."))

    def test_winter_cst_evening_stays_on_chicago_date(self) -> None:
        # 1 Jan 2026 23:30 CST is 2 Jan 2026 05:30 UTC.
        now = datetime(2026, 1, 2, 5, 30, tzinfo=timezone.utc)
        self.assertEqual(helper.next_version([], now=now), "2026.1.1.1")

    def test_no_zero_padding_on_month_or_day(self) -> None:
        now = datetime(2026, 8, 4, 17, 0, tzinfo=CHICAGO)
        version = helper.next_version([], now=now)
        self.assertEqual(version, "2026.8.4.1")
        self.assertNotEqual(version, "2026.08.04.1")
        self.assertNotIn(".0", version)

    def test_first_build_of_empty_chicago_day_is_one(self) -> None:
        now = datetime(2026, 8, 24, 18, 0, tzinfo=CHICAGO)
        tags = ["v2026.8.23.9", "v2026.3.12.4", "nightly", "v1.2.3"]
        self.assertEqual(helper.next_version(tags, now=now), "2026.8.24.1")

    def test_increments_from_existing_v_year_m_d_n_tags(self) -> None:
        now = datetime(2026, 8, 24, 18, 0, tzinfo=CHICAGO)
        tags = [
            "v2026.8.24.1",
            "refs/tags/v2026.8.24.3^{}",
            "v2026.8.24.2",
            "v2026.8.23.9",
        ]
        self.assertEqual(helper.next_version(tags, now=now), "2026.8.24.4")

    def test_accepts_tags_without_v_prefix_and_padded_components(self) -> None:
        now = datetime(2026, 8, 24, 18, 0, tzinfo=CHICAGO)
        tags = ["2026.8.24.1", "v2026.08.24.02"]
        self.assertEqual(helper.next_version(tags, now=now), "2026.8.24.3")

    def test_ignores_leftover_version_txt_contents(self) -> None:
        now = datetime(2026, 8, 25, 3, 30, tzinfo=timezone.utc)
        tags = ["v2026.8.24.1", "v2026.3.12.4"]
        # Helper API has no VERSION.txt parameter; leftover March files
        # must not become the published version.
        self.assertEqual(helper.next_version(tags, now=now), "2026.8.24.2")

    def test_ls_remote_output_strips_peeled_annotated_tags(self) -> None:
        output = (
            "aaaaaaaa\trefs/tags/v2026.8.24.1\n"
            "aaaaaaaa\trefs/tags/v2026.8.24.1^{}\n"
            "bbbbbbbb\trefs/tags/v2026.3.12.4\n"
        )
        tags = helper.tags_from_ls_remote_output(output)
        self.assertEqual(
            tags,
            ["v2026.8.24.1", "v2026.8.24.1", "v2026.3.12.4"],
        )

    def test_write_version_file_does_not_read_previous_contents(self) -> None:
        now = datetime(2026, 8, 24, 18, 0, tzinfo=CHICAGO)
        with tempfile.TemporaryDirectory() as tmp:
            version_file = Path(tmp) / "VERSION.txt"
            version_file.write_text("2026.3.12.4\n", encoding="ascii")
            version = helper.next_version(["v2026.8.24.1"], now=now)
            helper.write_version_file(version, version_file)
            self.assertEqual(version_file.read_text(encoding="ascii"), "2026.8.24.2\n")


class NextAppVersionCliTests(unittest.TestCase):
    def _run(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(HELPER_PATH), *args],
            check=False,
            capture_output=True,
            text=True,
        )

    def test_cli_assigns_first_build_on_empty_day(self) -> None:
        result = self._run(
            "--now",
            "2026-08-25T03:30:00+00:00",
            "--tags",
            "v2026.3.12.4",
            "v2026.8.23.1",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "2026.8.24.1")

    def test_cli_increments_existing_tag_and_writes_working_copy(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            version_file = Path(tmp) / "VERSION.txt"
            version_file.write_text("2026.3.12.4\n", encoding="ascii")
            result = self._run(
                "--now",
                "2026-08-24T23:00:00-05:00",
                "--tags",
                "v2026.8.24.1",
                "--write",
                "--version-file",
                str(version_file),
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), "2026.8.24.2")
            self.assertEqual(version_file.read_text(encoding="ascii"), "2026.8.24.2\n")

    def test_cli_evening_chicago_is_not_utc_tomorrow(self) -> None:
        result = self._run(
            "--now",
            "2026-08-25T04:59:00+00:00",
            "--tags",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "2026.8.24.1")


if __name__ == "__main__":
    unittest.main()
