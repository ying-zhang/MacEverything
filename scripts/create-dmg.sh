#!/bin/bash
set -euo pipefail

app_path="${1:-build/Release/MacEverything.app}"
dmg_path="${2:-MacEverything.dmg}"
volume_name="${3:-MacEverything}"

if [[ ! -d "$app_path" ]]; then
  echo "App bundle not found: $app_path" >&2
  exit 1
fi

staging_dir="$(mktemp -d "${TMPDIR:-/tmp}/maceverything-dmg.XXXXXX")"
cleanup() {
  rm -rf "$staging_dir"
}
trap cleanup EXIT

cp -R "$app_path" "$staging_dir/"
ln -s /Applications "$staging_dir/Applications"

hdiutil create -volname "$volume_name" \
  -srcfolder "$staging_dir" \
  -ov -format UDZO "$dmg_path"
