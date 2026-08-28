#!/usr/bin/env python3
"""Sign macOS zip archives and write per-arch Sparkle 2 appcasts.

Sparkle 2 does not support parallel same-version arm64 and x86_64 enclosures
in one feed (see sparkle-project/Sparkle#2701). Dedicated macos-15 and
macos-15-intel zips stay separate, and each architecture gets its own
appcast. Apple Silicon items set sparkle:hardwareRequirements to arm64.

Reads SPARKLE_PRIVATE_ED_KEY from the environment (publish job only).
Never prints the private key. Verifies the derived public key against the
committed SUPublicEDKey before writing feeds.
"""

from __future__ import annotations

import argparse
import base64
import os
import sys
from datetime import datetime, timezone
from pathlib import Path

PUBLIC_ED_KEY = "3OHeQ4AYE1Iwgz2MAhdoe/ZgwLal7rrnfnTmA9H8sqs="
REPO = "bstone108/matrix-media-archiver"
APP_NAME = "MatrixMediaArchiverQt"
SPARKLE_NS = "http://www.andymatuschak.org/xml-namespaces/sparkle"
DC_NS = "http://purl.org/dc/elements/1.1/"


def _load_signing_key(secret: str):
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
        from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
    except ImportError as exc:  # pragma: no cover - publish job installs cryptography
        raise SystemExit("cryptography is required to sign Sparkle updates") from exc

    raw = base64.b64decode(secret.strip())
    expected_public = base64.b64decode(PUBLIC_ED_KEY)
    if len(raw) == 32:
        seed = raw
    elif len(raw) == 64:
        seed = raw[:32]
    elif len(raw) == 96:
        seed = raw[:32]
    else:
        raise SystemExit(
            f"SPARKLE_PRIVATE_ED_KEY must decode to 32, 64, or 96 bytes; got {len(raw)}"
        )

    key = Ed25519PrivateKey.from_private_bytes(seed)
    derived = key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
    if derived != expected_public:
        raise SystemExit(
            "SPARKLE_PRIVATE_ED_KEY does not match the committed SUPublicEDKey"
        )
    return key


def ed_signature(key, data: bytes) -> str:
    return base64.b64encode(key.sign(data)).decode("ascii")


def xml_escape_url(url: str) -> str:
    return (
        url.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def rfc822(now: datetime | None = None) -> str:
    stamp = now or datetime.now(timezone.utc)
    return stamp.strftime("%a, %d %b %Y %H:%M:%S +0000")


def zip_url(version: str, arch: str) -> str:
    name = f"{APP_NAME}-{version}-macos-{arch}.zip"
    return f"https://github.com/{REPO}/releases/download/v{version}/{name}"


def release_html_url(version: str) -> str:
    return f"https://github.com/{REPO}/releases/tag/v{version}"


def write_appcast(
    path: Path,
    *,
    version: str,
    arch: str,
    signature: str,
    length: int,
    now: datetime | None = None,
) -> None:
    hardware = ""
    if arch == "arm64":
        hardware = "      <sparkle:hardwareRequirements>arm64</sparkle:hardwareRequirements>\n"
    body = f"""<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="{SPARKLE_NS}" xmlns:dc="{DC_NS}">
  <channel>
    <title>{APP_NAME}</title>
    <language>en</language>
    <item>
      <title>{APP_NAME} {version} ({arch})</title>
      <pubDate>{rfc822(now)}</pubDate>
      <link>{xml_escape_url(release_html_url(version))}</link>
      <sparkle:version>{version}</sparkle:version>
      <sparkle:shortVersionString>{version}</sparkle:shortVersionString>
      <sparkle:minimumSystemVersion>12.0.0</sparkle:minimumSystemVersion>
{hardware}      <enclosure url="{xml_escape_url(zip_url(version, arch))}"
                 sparkle:edSignature="{signature}"
                 length="{length}"
                 type="application/octet-stream" />
    </item>
  </channel>
</rss>
"""
    path.write_text(body, encoding="utf-8")


def sign_zip(key, zip_path: Path) -> tuple[str, int]:
    data = zip_path.read_bytes()
    return ed_signature(key, data), len(data)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--arm64-zip", type=Path, required=True)
    parser.add_argument("--x86_64-zip", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--private-key-env",
        default="SPARKLE_PRIVATE_ED_KEY",
        help="Environment variable holding the Sparkle private EdDSA key",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    secret = os.environ.get(args.private_key_env, "").strip()
    if not secret:
        print(f"{args.private_key_env} is not set", file=sys.stderr)
        return 1
    key = _load_signing_key(secret)

    for path in (args.arm64_zip, args.x86_64_zip):
        if not path.is_file():
            print(f"missing macOS zip: {path}", file=sys.stderr)
            return 1

    args.output_dir.mkdir(parents=True, exist_ok=True)
    arm_sig, arm_len = sign_zip(key, args.arm64_zip)
    intel_sig, intel_len = sign_zip(key, args.x86_64_zip)
    write_appcast(
        args.output_dir / "appcast-macos-arm64.xml",
        version=args.version,
        arch="arm64",
        signature=arm_sig,
        length=arm_len,
    )
    write_appcast(
        args.output_dir / "appcast-macos-x86_64.xml",
        version=args.version,
        arch="x86_64",
        signature=intel_sig,
        length=intel_len,
    )
    print(f"Wrote Sparkle appcasts for {args.version} to {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
