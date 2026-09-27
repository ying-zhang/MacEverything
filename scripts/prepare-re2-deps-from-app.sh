#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
app_path="${1:-/Applications/MacEverything.app}"
dest="${2:-$root_dir/third_party/re2}"
frameworks="$app_path/Contents/Frameworks"
re2_version="2025-11-05"
abseil_version="20260107.1"
re2_sha256="87f6029d2f6de8aa023654240a03ada90e876ce9a4676e258dd01ea4c26ffd67"
abseil_sha256="4314e2a7cbac89cac25a2f2322870f343d81579756ceff7f431803c2c9090195"
expected_re2_dylib="$frameworks/libre2.11.0.0.dylib"
expected_abseil_dylib="$frameworks/libabsl_base.2601.0.0.dylib"

if [[ ! -f "$expected_re2_dylib" ]]; then
  echo "error: expected RE2 $re2_version library not found: $expected_re2_dylib" >&2
  exit 66
fi
if [[ ! -f "$expected_abseil_dylib" ]]; then
  echo "error: expected Abseil $abseil_version library not found: $expected_abseil_dylib" >&2
  exit 66
fi
for dylib in "$expected_re2_dylib" "$expected_abseil_dylib"; do
  if ! lipo -archs "$dylib" | tr ' ' '\n' | grep -qx arm64; then
    echo "error: expected arm64 library: $dylib" >&2
    exit 65
  fi
done

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/maceverything-re2.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT
echo "Downloading official RE2 $re2_version and Abseil $abseil_version sources..."
curl -L --fail --show-error \
  --output "$work_dir/re2.tar.gz" \
  "https://codeload.github.com/google/re2/tar.gz/refs/tags/$re2_version"
curl -L --fail --show-error \
  --output "$work_dir/abseil.tar.gz" \
  "https://codeload.github.com/abseil/abseil-cpp/tar.gz/refs/tags/$abseil_version"

printf '%s  %s\n' "$re2_sha256" "$work_dir/re2.tar.gz" | shasum -a 256 -c -
printf '%s  %s\n' "$abseil_sha256" "$work_dir/abseil.tar.gz" | shasum -a 256 -c -
tar -xzf "$work_dir/re2.tar.gz" -C "$work_dir"
tar -xzf "$work_dir/abseil.tar.gz" -C "$work_dir"

RE2_SOURCE_DIR="$work_dir/re2-$re2_version" \
ABSEIL_SOURCE_DIR="$work_dir/abseil-cpp-$abseil_version" \
RE2_LIB_DIR="$frameworks" \
ABSEIL_LIB_DIR="$frameworks" \
  "$root_dir/scripts/prepare-re2-deps.sh" "$dest"

echo "Prepared Homebrew-free arm64 dependencies in $dest"
