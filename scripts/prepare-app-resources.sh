#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
app_path="${1:?usage: prepare-app-resources.sh /path/to/App.app}"
dependency_root="${RE2_DEPENDENCY_ROOT:-$root_dir/third_party/re2}"
for directory in "$app_path/Contents/Frameworks" "$app_path/Contents/MacOS"; do
  rm -f "$directory"/libre2*.dylib "$directory"/libabsl*.dylib
done
"$root_dir/scripts/verify-static-dependencies.sh" "$app_path"
mkdir -p "$app_path/Contents/Resources"
cp "$dependency_root/share/ThirdPartyLicenses.txt" "$app_path/Contents/Resources/ThirdPartyLicenses.txt"
