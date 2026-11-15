#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
app_path="${1:?usage: verify-release-app.sh /path/to/App.app [architecture]}"
arch="${2:-}"
"$root_dir/scripts/verify-static-dependencies.sh" "$app_path"
while IFS= read -r path; do
  file -b "$path" | grep -q 'Mach-O' || continue
  matches="$(strings "$path" | grep -E '/Users/[^/[:space:]]+/|/home/[^/[:space:]]+/' || true)"
  if [[ -n "$matches" ]]; then
    echo "error: personal build paths found in $path" >&2
    exit 65
  fi
done < <(find "$app_path/Contents" -type f)
codesign --verify --deep --strict "$app_path"
details="$(codesign -d --verbose=4 "$app_path" 2>&1)"
if ! printf '%s\n' "$details" | grep -Eq '^CodeDirectory .*flags=.*runtime'; then
  echo "error: hardened runtime is missing from $app_path" >&2
  exit 65
fi
entitlements="$(codesign -d --entitlements :- "$app_path" 2>/dev/null)"
if printf '%s\n' "$entitlements" | grep -q 'com.apple.security.cs.disable-library-validation'; then
  echo "error: library validation must remain enabled" >&2
  exit 65
fi
if [[ -n "$arch" ]]; then
  while IFS= read -r path; do
    file -b "$path" | grep -q 'Mach-O' || continue
    if ! lipo "$path" -verify_arch "$arch"; then
      echo "error: $path does not contain required architecture $arch" >&2
      exit 65
    fi
  done < <(find "$app_path/Contents" -type f)
fi
echo "Verified signatures, hardened runtime and static dependencies: $app_path"
