#!/bin/bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"

if rg -n 'github\.com/user/MacEverything|\.\./\.\./releases' \
    "$repo_root/README.md" "$repo_root/README_EN.md"; then
    echo "README contains placeholder or ambiguous release links" >&2
    exit 1
fi

duplicates="$({
    for file in "$repo_root"/docs/changelog/[0-9][0-9][0-9]-*.md; do
        base="$(basename "$file")"
        number="${base%%-*}"
        if ((10#$number >= 150)); then
            echo "$number"
        fi
    done
} | sort | uniq -d)"

if [[ -n "$duplicates" ]]; then
    echo "Duplicate changelog numbers at or above 150: $duplicates" >&2
    exit 1
fi

version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
    "$repo_root/MacEverything/Info.plist")"
if [[ "$version" != "0.0.0" ]] && \
   ! compgen -G "$repo_root/docs/changelog/*release-$version.md" >/dev/null; then
    echo "No release notes found for app version $version" >&2
    exit 1
fi

echo "Documentation checks passed"
