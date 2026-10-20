#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
artifact_dir="${ARTIFACT_DIR:-$root_dir/artifacts}"
configuration="${CONFIGURATION:-Release}"
scheme="${SCHEME:-MacEverything}"
volume_name="${VOLUME_NAME:-MacEverything}"
archs=("$@")
if (($# == 0)); then
  archs=(arm64 x86_64)
fi
mkdir -p "$artifact_dir"

for arch in "${archs[@]}"; do
  case "$arch" in
    arm64|x86_64) ;;
    *) echo "error: unsupported architecture: $arch" >&2; exit 64 ;;
  esac
  dep_root="$root_dir/build/deps/re2-$arch"
  RE2_ARCH="$arch" "$root_dir/scripts/prepare-re2-deps.sh" "$dep_root"
  symroot="$root_dir/build/$arch"
  derived_data="$root_dir/build/DerivedData-$arch"
  app_path="$symroot/$configuration/MacEverything.app"
  dmg_path="$artifact_dir/MacEverything-$arch.dmg"
  rm -rf "$symroot" "$derived_data"
  xcodebuild \
    -project "$root_dir/MacEverything.xcodeproj" \
    -scheme "$scheme" \
    -configuration "$configuration" \
    -destination "platform=macOS,arch=$arch" \
    -derivedDataPath "$derived_data" \
    SYMROOT="$symroot" \
    ARCHS="$arch" \
    ONLY_ACTIVE_ARCH=NO \
    RE2_DEPENDENCY_ROOT="$dep_root" \
    CODE_SIGN_STYLE=Manual \
    CODE_SIGN_IDENTITY=- \
    CODE_SIGN_ENTITLEMENTS="MacEverything/MacEverything.entitlements" \
    ENABLE_HARDENED_RUNTIME=YES \
    build
  for binary in MacEverything MacEverythingMCP mace; do
    if [[ ! -x "$app_path/Contents/MacOS/$binary" ]]; then
      echo "error: missing executable: $binary" >&2
      exit 65
    fi
  done
  "$root_dir/scripts/verify-release-app.sh" "$app_path" "$arch"
  rm -rf "$artifact_dir/MacEverything-$arch.app"
  cp -R "$app_path" "$artifact_dir/MacEverything-$arch.app"
  "$root_dir/scripts/create-dmg.sh" "$app_path" "$dmg_path" "$volume_name"
  echo "Built $dmg_path"
done
