#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import plistlib
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "release_versions", ROOT / "scripts/check-release-versions.py")
VERSIONS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERSIONS)


class ReleaseVersionTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        for directory in ("MacEverything", "docs/changelog", "scripts", "tests"):
            (self.root / directory).mkdir(parents=True)
        with (self.root / "MacEverything/Info.plist").open("wb") as stream:
            plistlib.dump({"CFBundleShortVersionString": "1.10.20"}, stream)
        for version in VERSIONS.ALLOWED_VERSIONS:
            (self.root / f"docs/changelog/release-{version}.md").write_text(version)
        for name in ("README.md", "README_EN.md"):
            (self.root / name).write_text("1.5.30 released; 1.10.20 pending")

    def test_current_versions_pass(self):
        self.assertEqual(VERSIONS.audit(self.root), [])

    def test_old_release_text_fails(self):
        version = ".".join(("1", "7", "25"))
        (self.root / "README.md").write_text(f"MacEverything v{version}")
        self.assertTrue(any("unsupported app version" in error
                            for error in VERSIONS.audit(self.root)))

    def test_old_release_filename_fails(self):
        version = ".".join(("1", "7", "25"))
        (self.root / f"docs/changelog/release-{version}.md").write_text("archive")
        self.assertTrue(any("unsupported release filename" in error
                            for error in VERSIONS.audit(self.root)))

    def test_stale_source_version_fails(self):
        version = ".".join(("1", "0", "0"))
        (self.root / "MacEverything/version.h").write_text(f'const char* version = "{version}";')
        self.assertTrue(any("unsupported app version" in error
                            for error in VERSIONS.audit(self.root)))

    def test_missing_link_fails(self):
        (self.root / "README.md").write_text("[release](docs/missing.md)")
        self.assertTrue(any("missing local link" in error
                            for error in VERSIONS.audit(self.root)))

    def test_external_and_non_app_versions_remain_valid(self):
        (self.root / "README.md").write_text(
            "CMake 3.22; MCP 2025-03-26; competitor 0.1.23; XML 1.0; 127.0.0.1\n"
            "[web](https://example.com/missing.md) [heading](#install)")
        self.assertEqual(VERSIONS.audit(self.root), [])

    def test_missing_release_notes_fails(self):
        (self.root / "docs/changelog/release-1.5.30.md").unlink()
        self.assertIn("missing release notes for 1.5.30", VERSIONS.audit(self.root))


if __name__ == "__main__":
    unittest.main()
