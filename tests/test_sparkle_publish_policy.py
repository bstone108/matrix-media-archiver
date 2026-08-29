#!/usr/bin/env python3
"""Guards: Sparkle private key and appcast generation stay on the publish path."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_macos_release_policy as policy

WORKFLOW = policy.WORKFLOW
INFO_PLIST = (policy.ROOT / "packaging/macos/Info.plist.in").read_text(encoding="utf-8")
PACKAGE_MACOS = policy.PACKAGE_MACOS
GENERATE_APPCAST = (
    policy.ROOT / "scripts/generate-sparkle-appcasts.py"
).read_text(encoding="utf-8")


class SparklePublishPolicyTests(unittest.TestCase):
    def test_private_key_secret_only_in_publish_release_job(self) -> None:
        jobs = policy._top_level_block(WORKFLOW, "jobs:")
        publish = policy._child_block(jobs, "publish-release:")
        self.assertIn("SPARKLE_PRIVATE_ED_KEY", publish)
        self.assertIn("secrets.SPARKLE_PRIVATE_ED_KEY", publish)
        self.assertIn("generate-sparkle-appcasts.py", publish)
        self.assertIn("appcast-macos-arm64.xml", GENERATE_APPCAST)
        self.assertIn("appcast-macos-x86_64.xml", GENERATE_APPCAST)

        for job_id in (
            "assign-version:",
            "build-linux:",
            "build-linux-arm64:",
            "build-windows:",
            "build-windows-arm64:",
            "build-macos:",
        ):
            block = policy._child_block(jobs, job_id)
            self.assertNotIn("SPARKLE_PRIVATE_ED_KEY", block, job_id)
            self.assertNotIn("generate-sparkle-appcasts.py", block, job_id)

        self.assertEqual(WORKFLOW.count("SPARKLE_PRIVATE_ED_KEY"), 2)
        self.assertEqual(WORKFLOW.count("generate-sparkle-appcasts.py"), 1)

    def test_publish_release_still_only_on_tag_or_workflow_dispatch(self) -> None:
        release_if = policy._job_if(WORKFLOW, "publish-release:")
        self.assertIn("workflow_dispatch", release_if)
        self.assertIn("startsWith(github.ref, 'refs/tags/v')", release_if)
        self.assertNotRegex(
            release_if,
            r"heads/main|heads/master|ref_name == 'main'|ref_name == 'master'",
        )
        self.assertNotIn("pull_request", release_if)

    def test_appcast_script_does_not_echo_private_key(self) -> None:
        self.assertNotIn("print(secret", GENERATE_APPCAST)
        self.assertNotIn("echo \"$SPARKLE_PRIVATE_ED_KEY\"", GENERATE_APPCAST)
        self.assertNotIn("echo $SPARKLE_PRIVATE_ED_KEY", GENERATE_APPCAST)
        self.assertIn("does not match the committed SUPublicEDKey", GENERATE_APPCAST)
        self.assertIn("sparkle:hardwareRequirements>arm64", GENERATE_APPCAST)
        self.assertNotIn("sparkle:os=", GENERATE_APPCAST)

    def test_info_plist_embeds_public_ed_key_only(self) -> None:
        self.assertIn("SUPublicEDKey", INFO_PLIST)
        self.assertIn("3OHeQ4AYE1Iwgz2MAhdoe/ZgwLal7rrnfnTmA9H8sqs=", INFO_PLIST)
        self.assertIn("SUFeedURL", INFO_PLIST)
        self.assertIn("appcast-macos-arm64.xml", INFO_PLIST)
        self.assertIn("SUEnableAutomaticChecks", INFO_PLIST)
        self.assertIn("<key>SUAutomaticallyUpdate</key>\n\t<false/>", INFO_PLIST)
        self.assertIn("SUScheduledCheckInterval", INFO_PLIST)
        self.assertIn("172800", INFO_PLIST)
        self.assertNotIn("SPARKLE_PRIVATE", INFO_PLIST)

    def test_package_macos_embeds_and_signs_sparkle_nested_code(self) -> None:
        self.assertIn("Sparkle.framework", PACKAGE_MACOS)
        self.assertIn("Signing XPC service", PACKAGE_MACOS)
        self.assertIn("Signing nested app", PACKAGE_MACOS)
        self.assertIn('[[ "${nested_app}" == "${app_bundle}" ]] && continue', PACKAGE_MACOS)


if __name__ == "__main__":
    unittest.main()
