#!/bin/bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"

if ! command -v rg >/dev/null 2>&1; then
    echo "error: documentation checks require ripgrep (rg)" >&2
    exit 69
fi

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

python3 "$repo_root/scripts/check-release-versions.py"

echo "Documentation checks passed"
