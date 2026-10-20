#!/usr/bin/env bash
set -euo pipefail

app_path="${1:?usage: verify-static-dependencies.sh /path/to/App.app}"
binary_count=0
while IFS= read -r path; do
  file -b "$path" | grep -q 'Mach-O' || continue
  binary_count=$((binary_count + 1))
  while IFS= read -r dependency; do
    case "$dependency" in
      /System/Library/*|/usr/lib/*) ;;
      *) echo "error: non-system dynamic dependency in $path: $dependency" >&2; exit 65 ;;
    esac
  done < <(otool -L "$path" | awk 'NR > 1 { print $1 }')
done < <(find "$app_path/Contents" -type f)

if [[ "$binary_count" -eq 0 ]]; then
  echo "error: no Mach-O binaries found in $app_path" >&2
  exit 65
fi
