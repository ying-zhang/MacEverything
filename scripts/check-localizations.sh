#!/bin/bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
english="$repo_root/MacEverything/en.lproj/Localizable.strings"
chinese="$repo_root/MacEverything/zh-Hans.lproj/Localizable.strings"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/mace-localizations.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT

plutil -lint "$english" "$chinese" >/dev/null

extract_keys() {
    perl -ne 'while (/^\s*"((?:\\.|[^"\\])*)"\s*=/g) { print "$1\n" }' "$1" | sort -u
}

extract_keys "$english" > "$work_dir/en"
extract_keys "$chinese" > "$work_dir/zh"

if ! diff -u "$work_dir/en" "$work_dir/zh"; then
    echo "Localization key sets differ" >&2
    exit 1
fi

perl -ne 'while (/(?:L10n\.tr|NSLocalizedString)\("((?:\\.|[^"\\])*)"/g) { print "$1\n" }' \
    "$repo_root"/MacEverything/App/*.swift | sort -u > "$work_dir/used"

missing="$(comm -23 "$work_dir/used" "$work_dir/en")"
if [[ -n "$missing" ]]; then
    echo "Missing localization keys:" >&2
    echo "$missing" >&2
    exit 1
fi

echo "Localization checks passed"
