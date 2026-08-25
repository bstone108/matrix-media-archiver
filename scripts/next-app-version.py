#!/usr/bin/env python3
"""Compute the next MatrixMediaArchiverQt date.build version.

Format is year.month.day.build with no zero-padding. The calendar date is
always America/Chicago. The last component is 1 for the first build of that
Chicago day and then increments past any existing git tags or GitHub
releases named vYEAR.M.D.N.

This helper does not read VERSION.txt. That file is a local/dev fallback
and is left alone on pull-request CI. Desktop CI calls this helper only
on the publish path (push to main/master or workflow_dispatch), then
stamps the computed version into the runner working copy. CI must not
commit VERSION.txt.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from collections.abc import Iterable, Sequence
from datetime import datetime, timezone
from pathlib import Path
from zoneinfo import ZoneInfo

CHICAGO = ZoneInfo("America/Chicago")
VERSION_TAG_PATTERN = re.compile(r"^v?(\d+)\.(\d+)\.(\d+)\.(\d+)$")
REPO_ROOT = Path(__file__).resolve().parent.parent


def chicago_datetime(now: datetime | None = None) -> datetime:
    if now is None:
        now = datetime.now(timezone.utc)
    elif now.tzinfo is None:
        now = now.replace(tzinfo=timezone.utc)
    return now.astimezone(CHICAGO)


def chicago_date_components(now: datetime | None = None) -> tuple[int, int, int]:
    local = chicago_datetime(now)
    return local.year, local.month, local.day


def format_version(year: int, month: int, day: int, build: int) -> str:
    if min(year, month, day, build) < 1:
        raise ValueError(f"invalid version components: {year}.{month}.{day}.{build}")
    return f"{year}.{month}.{day}.{build}"


def normalize_tag_name(raw: str) -> str:
    name = raw.strip()
    if name.startswith("refs/tags/"):
        name = name[len("refs/tags/") :]
    if name.endswith("^{}"):
        name = name[: -len("^{}")]
    return name.strip()


def parse_version_tag(raw: str) -> tuple[int, int, int, int] | None:
    name = normalize_tag_name(raw)
    match = VERSION_TAG_PATTERN.fullmatch(name)
    if match is None:
        return None
    year, month, day, build = (int(part) for part in match.groups())
    if min(year, month, day, build) < 1:
        return None
    return year, month, day, build


def max_build_for_date(
    tags: Iterable[str], year: int, month: int, day: int
) -> int:
    highest = 0
    for tag in tags:
        parsed = parse_version_tag(tag)
        if parsed is None:
            continue
        tag_year, tag_month, tag_day, tag_build = parsed
        if (tag_year, tag_month, tag_day) == (year, month, day):
            highest = max(highest, tag_build)
    return highest


def next_version(tags: Iterable[str], now: datetime | None = None) -> str:
    year, month, day = chicago_date_components(now)
    build = max_build_for_date(tags, year, month, day) + 1
    return format_version(year, month, day, build)


def tags_from_ls_remote_output(output: str) -> list[str]:
    tags: list[str] = []
    for line in output.splitlines():
        line = line.strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        tags.append(normalize_tag_name(parts[-1]))
    return tags


def _run_git(args: Sequence[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args],
        cwd=str(cwd),
        capture_output=True,
        text=True,
        check=False,
    )


def collect_local_git_tags(repo_root: Path) -> list[str]:
    result = _run_git(["tag", "--list"], repo_root)
    if result.returncode != 0:
        return []
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def collect_remote_git_tags(repo_root: Path) -> tuple[list[str], bool]:
    result = _run_git(["ls-remote", "--tags", "origin"], repo_root)
    if result.returncode != 0:
        return [], False
    return tags_from_ls_remote_output(result.stdout), True


def collect_github_release_tags() -> tuple[list[str], bool]:
    if shutil.which("gh") is None:
        return [], False
    result = subprocess.run(
        ["gh", "release", "list", "--limit", "1000", "--json", "tagName"],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return [], False
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError:
        return [], False
    if not isinstance(payload, list):
        return [], False
    tags: list[str] = []
    for item in payload:
        if isinstance(item, dict) and item.get("tagName"):
            tags.append(str(item["tagName"]))
    return tags, True


def collect_published_tags(
    repo_root: Path | None = None, *, strict: bool = False
) -> list[str]:
    root = repo_root or REPO_ROOT
    tags: list[str] = []
    remote_ok = False

    remote_tags, remote_listed = collect_remote_git_tags(root)
    if remote_listed:
        remote_ok = True
        tags.extend(remote_tags)

    release_tags, releases_listed = collect_github_release_tags()
    if releases_listed:
        remote_ok = True
        tags.extend(release_tags)

    tags.extend(collect_local_git_tags(root))

    if strict and not remote_ok:
        raise RuntimeError(
            "Unable to list remote git tags or GitHub releases; "
            "refusing to guess the next date.build number"
        )
    return tags


def write_version_file(version: str, version_file: Path) -> None:
    version_file.write_text(f"{version}\n", encoding="ascii")


def parse_now(value: str) -> datetime:
    now = datetime.fromisoformat(value)
    if now.tzinfo is None:
        now = now.replace(tzinfo=timezone.utc)
    return now


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--write",
        action="store_true",
        help="Write the computed version into VERSION.txt (working copy only)",
    )
    parser.add_argument(
        "--version-file",
        type=Path,
        default=None,
        help="VERSION.txt path used with --write (defaults to repo root)",
    )
    parser.add_argument(
        "--now",
        help="ISO-8601 timestamp override; naive values are treated as UTC",
    )
    parser.add_argument(
        "--tags",
        nargs="*",
        default=None,
        help="Use these tag names instead of querying git/GitHub",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Fail if remote git tags and GitHub releases cannot be listed",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=REPO_ROOT,
        help="Repository root used to query git tags",
    )
    args = parser.parse_args(argv)

    now = parse_now(args.now) if args.now else None
    if args.tags is not None:
        tags = list(args.tags)
    else:
        try:
            tags = collect_published_tags(args.repo_root, strict=args.strict)
        except RuntimeError as exc:
            print(str(exc), file=sys.stderr)
            return 1

    version = next_version(tags, now=now)
    if args.write:
        version_file = args.version_file or (args.repo_root / "VERSION.txt")
        write_version_file(version, version_file)
    print(version)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
