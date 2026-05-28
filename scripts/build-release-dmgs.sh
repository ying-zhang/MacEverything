#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
artifact_dir="${ARTIFACT_DIR:-$root_dir/artifacts}"
configuration="${CONFIGURATION:-Release}"
scheme="${SCHEME:-MacEverything}"
volume_name="${VOLUME_NAME:-MacEverything}"

if (($# > 0)); then
  archs=("$@")
else
  archs=(arm64 x86_64)
fi

mkdir -p "$artifact_dir"

brew_prefix_for_arch() {
  case "$1" in
    arm64) echo "${ARM64_BREW_PREFIX:-/opt/homebrew}" ;;
    x86_64) echo "${X86_64_BREW_PREFIX:-/usr/local}" ;;
    *) echo "error: unsupported architecture: $1" >&2; exit 64 ;;
  esac
}

assert_dylib_arch() {
  local arch="$1"
  local path="$2"
  if ! lipo -info "$path" | grep -Eq "(Non-fat file: .* is architecture: $arch|Architectures in the fat file: .*:.*\b$arch\b)"; then
    echo "error: $path does not contain required architecture $arch" >&2
    lipo -info "$path" >&2 || true
    exit 65
  fi
}

prepare_re2_for_arch() {
  local arch="$1"
  local prefix
  prefix="$(brew_prefix_for_arch "$arch")"
  local dep_root="$root_dir/build/deps/re2-$arch"
  local default_dep_root="$root_dir/third_party/re2"

  if [[ -d "$default_dep_root/include/re2" && -d "$default_dep_root/include/absl" ]] &&
      find "$default_dep_root/lib" -maxdepth 1 -name 'libre2*.dylib' -print -quit | grep -q . &&
      find "$default_dep_root/lib" -maxdepth 1 -name 'libabsl*.dylib' -print -quit | grep -q .; then
    local default_matches=1
    local dylib
    while IFS= read -r dylib; do
      if ! lipo -info "$dylib" | grep -Eq "(Non-fat file: .* is architecture: $arch|Architectures in the fat file: .*:.*\b$arch\b)"; then
        default_matches=0
        break
      fi
    done < <(find "$default_dep_root/lib" -maxdepth 1 -name '*.dylib' -print)

    if [[ $default_matches -eq 1 ]]; then
      printf '%s' "$default_dep_root"
      return 0
    fi
  fi

  if [[ ! -d "$prefix/opt/re2" || ! -d "$prefix/opt/abseil" ]]; then
    cat >&2 <<EOF
error: missing $arch Homebrew re2/abseil dependencies under $prefix.
Install them first, for example:
  $( [[ "$arch" == arm64 ]] && echo "arch -arm64" || echo "arch -x86_64" ) brew install re2 abseil

Override the prefix with ARM64_BREW_PREFIX or X86_64_BREW_PREFIX if needed.
EOF
    exit 69
  fi

  RE2_PREFIX="$prefix/opt/re2" \
    ABSEIL_PREFIX="$prefix/opt/abseil" \
    "$root_dir/scripts/prepare-re2-deps.sh" "$dep_root" >&2

  local dylib
  while IFS= read -r dylib; do
    assert_dylib_arch "$arch" "$dylib"
  done < <(find "$dep_root/lib" -maxdepth 1 -name '*.dylib' -print)

  printf '%s' "$dep_root"
}

verify_app_arch() {
  local arch="$1"
  local app_path="$2"
  local main="$app_path/Contents/MacOS/MacEverything"
  local mcp="$app_path/Contents/MacOS/MacEverythingMCP"

  assert_dylib_arch "$arch" "$main"
  assert_dylib_arch "$arch" "$mcp"

  if otool -L "$main" | grep -E '/opt/homebrew|/usr/local'; then
    echo "error: app binary still references Homebrew dylibs" >&2
    exit 65
  fi

  if ! find "$app_path/Contents/Frameworks" -name 'libre2*.dylib' -print -quit | grep -q .; then
    echo "error: libre2 dylib was not embedded in the app bundle" >&2
    exit 65
  fi

  local dylib
  while IFS= read -r dylib; do
    assert_dylib_arch "$arch" "$dylib"
    if otool -L "$dylib" | grep -E '/opt/homebrew|/usr/local'; then
      echo "error: $dylib still references Homebrew dylibs" >&2
      exit 65
    fi
  done < <(find "$app_path/Contents/Frameworks" -name '*.dylib' -print)
}

build_arch() {
  local arch="$1"
  local dep_root="$2"
  local symroot="$root_dir/build/$arch"
  local derived_data="$root_dir/build/DerivedData-$arch"
  local app_path="$symroot/$configuration/MacEverything.app"
  local dmg_path="$artifact_dir/MacEverything-$arch.dmg"

  rm -rf "$symroot" "$derived_data"

  xcodebuild \
    -project "$root_dir/MacEverything.xcodeproj" \
    -scheme "$scheme" \
    -configuration "$configuration" \
    -derivedDataPath "$derived_data" \
    SYMROOT="$symroot" \
    ARCHS="$arch" \
    ONLY_ACTIVE_ARCH=NO \
    RE2_DEPENDENCY_ROOT="$dep_root" \
    CODE_SIGN_STYLE=Manual \
    CODE_SIGN_IDENTITY="-" \
    DEVELOPMENT_TEAM="" \
    build

  verify_app_arch "$arch" "$app_path"
  "$root_dir/scripts/create-dmg.sh" "$app_path" "$dmg_path" "$volume_name"
  echo "Built $dmg_path"
}

for arch in "${archs[@]}"; do
  echo "==> Building $arch DMG"
  dep_root="$(prepare_re2_for_arch "$arch")"
  build_arch "$arch" "$dep_root"
done
