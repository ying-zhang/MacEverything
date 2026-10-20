#!/usr/bin/env python3
import os
import json
from pathlib import Path
import platform
import plistlib
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
VERIFY = ROOT / "scripts/verify-release-app.sh"
DEPENDENCIES = Path(os.environ.get("RE2_PREFIX", ROOT / "third_party/re2"))


@unittest.skipUnless(platform.system() == "Darwin" and shutil.which("clang++"),
                     "requires macOS compiler and signing tools")
class ReleaseSigningTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="mace signing ")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.app = self.root / "Relocated App.app"
        self.binary = self.app / "Contents/MacOS/SigningFixture"
        self.binary.parent.mkdir(parents=True)
        with (self.app / "Contents/Info.plist").open("wb") as plist:
            plistlib.dump({
                "CFBundleIdentifier": "org.example.signingfixture",
                "CFBundleExecutable": "SigningFixture",
                "CFBundlePackageType": "APPL",
                "CFBundleShortVersionString": "1.10.20",
            }, plist)
        self.source = self.root / "main.cpp"

    def run_command(self, command):
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result

    def compile(self, source="int main() { return 0; }", libraries=()):
        self.source.write_text(source)
        self.run_command([
            "clang++", "-arch", platform.machine(), "-std=c++20", str(self.source),
            "-I", str(DEPENDENCIES / "include"),
            *map(str, libraries), "-framework", "CoreFoundation", "-o", str(self.binary),
        ])

    def sign(self, runtime=True):
        command = ["codesign", "--force", "--sign", "-"]
        if runtime:
            command += ["--options", "runtime"]
        self.run_command(command + [str(self.app)])

    def verify(self, arch=None):
        command = [str(VERIFY), str(self.app)]
        if arch:
            command.append(arch)
        return subprocess.run(command, capture_output=True, text=True)

    def test_hardened_static_re2_app_launches_after_relocation(self):
        archive = DEPENDENCIES / "lib/libre2_bundle.a"
        self.assertTrue(archive.is_file(), "run scripts/prepare-re2-deps.sh first")
        self.compile(
            '#include <re2/re2.h>\n'
            'int main() { return RE2::FullMatch("file123.txt", '
            '"file[0-9]+\\\\.txt") ? 0 : 1; }', [archive])
        self.sign()
        self.assertEqual(self.verify(platform.machine()).returncode, 0)
        relocated = self.root / "Another Location" / self.app.name
        relocated.parent.mkdir()
        shutil.move(self.app, relocated)
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith("DYLD_")}
        result = subprocess.run(
            [str(relocated / "Contents/MacOS/SigningFixture")],
            cwd="/", env=environment, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_signature_integrity_does_not_guarantee_library_loadability(self):
        library = self.app / "Contents/Frameworks/libre2.11.dylib"
        library.parent.mkdir()
        library_source = self.root / "library.cpp"
        library_source.write_text('extern "C" int fixture_value() { return 73; }')
        self.run_command([
            "clang++", "-arch", platform.machine(), "-dynamiclib", str(library_source), "-o", str(library),
            "-Wl,-install_name,@rpath/libre2.11.dylib",
        ])
        self.run_command(["codesign", "--force", "--sign", "-", str(library)])
        self.compile('extern "C" int fixture_value(); '
                     'int main() { return fixture_value() == 73 ? 0 : 1; }',
                     [library, "-Wl,-rpath,@executable_path/../Frameworks"])
        self.sign()
        self.run_command(["codesign", "--verify", "--deep", "--strict", str(self.app)])
        result = self.verify()
        self.assertEqual(result.returncode, 65, result.stderr)
        self.assertIn("non-system dynamic dependency", result.stderr)

    def test_missing_hardened_runtime_is_rejected(self):
        self.compile()
        self.sign(runtime=False)
        result = self.verify()
        self.assertEqual(result.returncode, 65, result.stderr)
        self.assertIn("hardened runtime is missing", result.stderr)

    def test_packaged_mcp_reports_bundle_version(self):
        self.run_command([
            "clang++", "-arch", platform.machine(), "-std=c++20", "-fobjc-arc",
            str(ROOT / "MacEverything/CLI/mcp_main.mm"), "-framework", "Foundation",
            "-o", str(self.binary),
        ])
        self.sign()
        self.assertEqual(self.verify().returncode, 0)
        result = subprocess.run(
            [str(self.binary)], input=json.dumps({
                "jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {},
            }) + "\n", capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        response = json.loads(result.stdout)
        self.assertEqual(response["result"]["serverInfo"]["version"], "1.10.20")
        standalone = self.root / "StandaloneMCP"
        shutil.copy2(self.binary, standalone)
        result = subprocess.run(
            [str(standalone)], input=json.dumps({
                "jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {},
            }) + "\n", capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        response = json.loads(result.stdout)
        self.assertEqual(response["result"]["serverInfo"]["version"], "unknown")

    def test_incremental_build_removes_legacy_libraries(self):
        self.compile()
        frameworks = self.app / "Contents/Frameworks"
        frameworks.mkdir()
        stale_libraries = [frameworks / "libre2.11.dylib",
                           frameworks / "libabsl_base.dylib"]
        for library in stale_libraries:
            library.write_bytes(b"stale build output")
        environment = dict(os.environ, RE2_DEPENDENCY_ROOT=str(DEPENDENCIES))
        result = subprocess.run(
            [str(ROOT / "scripts/prepare-app-resources.sh"), str(self.app)],
            env=environment, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(all(not library.exists() for library in stale_libraries))
        self.assertTrue((self.app / "Contents/Resources/ThirdPartyLicenses.txt").is_file())
        self.sign()
        self.assertEqual(self.verify().returncode, 0)

    def test_tampered_bundle_is_rejected(self):
        self.compile()
        self.sign()
        (self.app / "Contents/Info.plist").write_bytes(b"invalid plist")
        self.assertNotEqual(self.verify().returncode, 0)

    def test_wrong_architecture_is_rejected(self):
        self.compile()
        self.sign()
        other_arch = "x86_64" if platform.machine() == "arm64" else "arm64"
        self.assertNotEqual(self.verify(other_arch).returncode, 0)


if __name__ == "__main__":
    unittest.main()
