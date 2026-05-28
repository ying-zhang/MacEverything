#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 /path/to/MacEverything.app [binary-name ...]" >&2
  exit 64
fi

APP_PATH="$1"
shift

if [[ ! -d "$APP_PATH/Contents/MacOS" ]]; then
  echo "error: app bundle not found: $APP_PATH" >&2
  exit 66
fi

FRAMEWORKS_DIR="$APP_PATH/Contents/Frameworks"
mkdir -p "$FRAMEWORKS_DIR"

if (( $# > 0 )); then
  BINARIES=("$@")
else
  BINARIES=("MacEverything")
fi

declare -a QUEUE=()
for candidate in \
  "${RE2_DEPENDENCY_ROOT:-}/lib/libre2"*.dylib \
  "${RE2_DEPENDENCY_ROOT:-}/lib/libabsl"*.dylib \
  "${SRCROOT:-}/third_party/re2/lib/libre2"*.dylib \
  "${SRCROOT:-}/third_party/re2/lib/libabsl"*.dylib \
  /opt/homebrew/opt/re2/lib/libre2*.dylib \
  /usr/local/opt/re2/lib/libre2*.dylib; do
  if [[ -f "$candidate" ]]; then
    QUEUE+=("$candidate")
  fi
done

if ((${#QUEUE[@]} == 0)); then
  echo "error: libre2 dylib not found. Install it with: brew install re2" >&2
  exit 69
fi

COPIED_NAMES=""

is_embeddable_dependency() {
  local dep="$1"
  [[ "$dep" == "$FRAMEWORKS_DIR"/* ]] ||
    [[ -n "${RE2_DEPENDENCY_ROOT:-}" && "$dep" == "$RE2_DEPENDENCY_ROOT/lib/"* ]] ||
    [[ -n "${SRCROOT:-}" && "$dep" == "$SRCROOT/third_party/re2/lib/"* ]] ||
    [[ "$dep" == /opt/homebrew/* || "$dep" == /usr/local/* ]]
}

has_copied_name() {
  local name="$1"
  [[ "$COPIED_NAMES" == *"
$name
"* ]]
}

copy_dependency_closure() {
  while ((${#QUEUE[@]})); do
    local dep="${QUEUE[0]}"
    QUEUE=("${QUEUE[@]:1}")

    [[ -f "$dep" ]] || continue
    local name
    name="$(basename "$dep")"
    has_copied_name "$name" && continue
    COPIED_NAMES="$COPIED_NAMES
$name
"

    cp -fL "$dep" "$FRAMEWORKS_DIR/$name"
    chmod u+w "$FRAMEWORKS_DIR/$name"

    while IFS= read -r child; do
      if is_embeddable_dependency "$child"; then
        QUEUE+=("$child")
      fi
    done < <(otool -L "$dep" | awk 'NR > 1 { print $1 }')
  done
}

rewrite_binary_references() {
  local binary="$1"
  local path="$APP_PATH/Contents/MacOS/$binary"
  [[ -f "$path" ]] || return 0

  while IFS= read -r dep; do
    if is_embeddable_dependency "$dep"; then
      install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$path"
    fi
  done < <(otool -L "$path" | awk 'NR > 1 { print $1 }')

  codesign --force --sign - "$path" >/dev/null
}

rewrite_dylib_references() {
  local dylib
  for dylib in "$FRAMEWORKS_DIR"/*.dylib; do
    [[ -f "$dylib" ]] || continue
    install_name_tool -id "@rpath/$(basename "$dylib")" "$dylib"

    while IFS= read -r dep; do
      if is_embeddable_dependency "$dep"; then
        install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$dylib"
      fi
    done < <(otool -L "$dylib" | awk 'NR > 1 { print $1 }')

    codesign --force --sign - "$dylib" >/dev/null
  done
}

copy_dependency_closure
for binary in "${BINARIES[@]}"; do
  rewrite_binary_references "$binary"
done
rewrite_dylib_references

echo "Embedded Homebrew dylibs in $FRAMEWORKS_DIR:"
find "$FRAMEWORKS_DIR" -maxdepth 1 -name '*.dylib' -print | sort
