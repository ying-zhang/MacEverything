#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="${1:-$root_dir/third_party/re2}"
arch="${RE2_ARCH:-$(uname -m)}"
deployment_target=15.0
re2_version=2025-11-05
abseil_version=20260107.1
source_root="$root_dir/build/dependency-sources"
build_root="$root_dir/build/deps/cmake-$arch"
manifest="static-v2 $arch $deployment_target $re2_version $abseil_version"

case "$arch" in
  arm64|x86_64) ;;
  *) echo "error: unsupported architecture: $arch" >&2; exit 64 ;;
esac

if [[ -f "$dest/manifest.txt" && "$(cat "$dest/manifest.txt")" == "$manifest" &&
      -f "$dest/lib/libre2_bundle.a" && -d "$dest/include/re2" &&
      -d "$dest/include/absl" && -f "$dest/share/ThirdPartyLicenses.txt" ]]; then
  echo "Static RE2 dependencies already prepared in $dest"
  exit 0
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "error: cmake is required to build static RE2/Abseil dependencies" >&2
  exit 69
fi

mkdir -p "$source_root" "$(dirname "$dest")"
dest="$(cd "$(dirname "$dest")" && pwd)/$(basename "$dest")"
download_source() {
  local name="$1" url="$2" checksum="$3"
  local archive="$source_root/$name.tar.gz"
  if [[ ! -f "$archive" ]]; then
    /usr/bin/nscurl --output "$archive.download" "$url" >&2
    printf '%s  %s\n' "$checksum" "$archive.download" | shasum -a 256 -c - >&2
    mv "$archive.download" "$archive"
  fi
  printf '%s  %s\n' "$checksum" "$archive" | shasum -a 256 -c - >&2
  if [[ ! -d "$source_root/$name" ]]; then
    tar -xzf "$archive" -C "$source_root"
  fi
}

download_source "re2-$re2_version" \
  "https://codeload.github.com/google/re2/tar.gz/refs/tags/$re2_version" \
  87f6029d2f6de8aa023654240a03ada90e876ce9a4676e258dd01ea4c26ffd67
download_source "abseil-cpp-$abseil_version" \
  "https://codeload.github.com/abseil/abseil-cpp/tar.gz/refs/tags/$abseil_version" \
  4314e2a7cbac89cac25a2f2322870f343d81579756ceff7f431803c2c9090195

staging="$(mktemp -d "$dest.tmp.XXXXXX")"
trap 'rm -rf "$staging"' EXIT
cmake -S "$root_dir/scripts/re2" -B "$build_root" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_CXX_FLAGS="-ffile-prefix-map=$root_dir=." \
  -DCMAKE_OSX_ARCHITECTURES="$arch" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" \
  -DCMAKE_INSTALL_PREFIX="$staging" \
  -DRE2_SOURCE_DIR="$source_root/re2-$re2_version" \
  -DABSEIL_SOURCE_DIR="$source_root/abseil-cpp-$abseil_version"
cmake --build "$build_root" --parallel "${BUILD_JOBS:-$(sysctl -n hw.ncpu)}"
cmake --install "$build_root"
/usr/bin/libtool -static -o "$staging/lib/libre2_bundle.a" "$staging"/lib/*.a
mkdir -p "$staging/share"
{
  printf 'RE2 %s\n\n' "$re2_version"
  cat "$source_root/re2-$re2_version/LICENSE"
  printf '\nAbseil %s\n\n' "$abseil_version"
  cat "$source_root/abseil-cpp-$abseil_version/LICENSE"
} > "$staging/share/ThirdPartyLicenses.txt"
printf '%s\n' "$manifest" > "$staging/manifest.txt"
rm -rf "$dest"
mv "$staging" "$dest"
echo "Prepared static $arch RE2/Abseil dependencies in $dest"
