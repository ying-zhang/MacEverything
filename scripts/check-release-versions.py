#!/usr/bin/env python3
from pathlib import Path
import plistlib
import re
import sys
from urllib.parse import unquote, urlsplit


ALLOWED_VERSIONS = {"1.5.30", "1.10.20"}
VERSION_PATTERN = re.compile(r"(?<![\w.])v?1\.\d+\.\d+(?![\w.])")
TEXT_SUFFIXES = {".md", ".h", ".cpp", ".mm", ".swift", ".plist", ".py", ".sh"}


def audit(root):
    errors = []
    plist = root / "MacEverything/Info.plist"
    with plist.open("rb") as stream:
        version = plistlib.load(stream)["CFBundleShortVersionString"]
    if version != "1.10.20":
        errors.append(f"{plist.relative_to(root)}: unexpected app version {version}")
    paths = [root / "README.md", root / "README_EN.md"]
    for directory in ("MacEverything", "docs", "scripts", "tests"):
        paths.extend(path for path in (root / directory).rglob("*")
                     if path.is_file() and path.suffix in TEXT_SUFFIXES)
    for path in sorted(paths):
        relative = path.relative_to(root)
        text = path.read_text(encoding="utf-8")
        for number, line in enumerate(text.splitlines(), 1):
            for match in VERSION_PATTERN.finditer(line):
                if match.group().lstrip("v") not in ALLOWED_VERSIONS:
                    errors.append(f"{relative}:{number}: unsupported app version {match.group()}")
        if path.suffix != ".md":
            continue
        release = re.search(r"release-(\d+(?:\.\d+)+)", path.name)
        if release and release.group(1) not in ALLOWED_VERSIONS:
            errors.append(f"{relative}: unsupported release filename")
        for match in re.finditer(r"!?\[[^\]]*\]\(([^)\s]+)\)", text):
            target = urlsplit(match.group(1))
            if target.scheme or target.netloc or not target.path:
                continue
            destination = path.parent / unquote(target.path)
            if not destination.exists():
                number = text.count("\n", 0, match.start()) + 1
                errors.append(f"{relative}:{number}: missing local link {target.path}")
    for release in ALLOWED_VERSIONS:
        if not list((root / "docs/changelog").glob(f"*release-{release}.md")):
            errors.append(f"missing release notes for {release}")
    return errors


def main():
    errors = audit(Path(__file__).resolve().parents[1])
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("Release versions and local Markdown links verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
