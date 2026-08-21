#!/bin/bash
set -euo pipefail

usage() {
    echo "Usage: $0 <version> <arm64-dmg> <x86_64-dmg>" >&2
    echo "Set HOMEBREW_TAP_DIR to override the default sibling tap directory." >&2
}

if [[ $# -ne 3 ]]; then
    usage
    exit 2
fi

version="${1#v}"
arm64_dmg="$2"
intel_dmg="$3"

if [[ -z "$version" || ! "$version" =~ ^[0-9][0-9A-Za-z._-]*$ ]]; then
    echo "Invalid version: $1" >&2
    exit 2
fi

for dmg in "$arm64_dmg" "$intel_dmg"; do
    if [[ ! -f "$dmg" ]]; then
        echo "DMG not found: $dmg" >&2
        exit 2
    fi
done

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
tap_dir="${HOMEBREW_TAP_DIR:-$(cd "$repo_root/.." && pwd)/homebrew-maceverything}"
cask_file="$tap_dir/Casks/maceverything.rb"

if [[ ! -f "$cask_file" ]]; then
    echo "Cask not found: $cask_file" >&2
    exit 2
fi

arm64_sha="$(shasum -a 256 "$arm64_dmg" | awk '{print $1}')"
intel_sha="$(shasum -a 256 "$intel_dmg" | awk '{print $1}')"

temporary_file="$(mktemp "${TMPDIR:-/tmp}/maceverything-cask.XXXXXX")"
trap 'rm -f "$temporary_file"' EXIT

sed -E \
    -e "s|^  version \"[^\"]+\"|  version \"$version\"|" \
    -e "s|^  sha256 arm:[[:space:]]+\"[^\"]+\",|  sha256 arm:   \"$arm64_sha\",|" \
    -e "s|^         intel: \"[^\"]+\"|         intel: \"$intel_sha\"|" \
    "$cask_file" > "$temporary_file"

updated_version="$(sed -nE 's/^  version "([^"]+)"$/\1/p' "$temporary_file")"
updated_arm64_sha="$(sed -nE 's/^  sha256 arm:[[:space:]]+"([^"]+)",$/\1/p' "$temporary_file")"
updated_intel_sha="$(sed -nE 's/^         intel: "([^"]+)"$/\1/p' "$temporary_file")"
if [[ "$updated_version" != "$version" ||
      "$updated_arm64_sha" != "$arm64_sha" ||
      "$updated_intel_sha" != "$intel_sha" ]]; then
    echo "Failed to update the expected cask fields" >&2
    exit 1
fi

mv "$temporary_file" "$cask_file"
trap - EXIT

echo "Updated $cask_file"
echo "arm64 sha256:  $arm64_sha"
echo "x86_64 sha256: $intel_sha"
echo
cat "$cask_file"
