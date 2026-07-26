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
  BINARIES=("MacEverything" "MacEverythingMCP" "mace")
fi

# Seed only with libre2; copy_dependency_closure() walks otool -L recursively to
# embed exactly the Abseil dylibs libre2 actually references at runtime. Seeding
# every libabsl*.dylib here would embed Homebrew's entire Abseil set (~95 libs,
# including test-only ones like libabsl_scoped_mock_log), bloating the bundle.
declare -a QUEUE=()
for candidate in \
  "${RE2_DEPENDENCY_ROOT:-}/lib/libre2"*.dylib \
  "${SRCROOT:-}/third_party/re2/lib/libre2"*.dylib \
  /opt/homebrew/opt/re2/lib/libre2*.dylib \
  /usr/local/opt/re2/lib/libre2*.dylib; do
  if [[ -f "$candidate" ]]; then
    QUEUE+=("$candidate")
  fi
done

if ((${#QUEUE[@]} == 0)); then
  echo "error: libre2 dylib not found under RE2_DEPENDENCY_ROOT or a supported package-manager prefix" >&2
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

resolve_embeddable_dependency() {
  local dep="$1"

  if is_embeddable_dependency "$dep"; then
    printf '%s\n' "$dep"
    return 0
  fi

  case "$dep" in
    @rpath/*.dylib|@loader_path/*.dylib|@executable_path/*.dylib)
      local name
      name="$(basename "$dep")"
      local root
      for root in \
        "$FRAMEWORKS_DIR" \
        "${RE2_DEPENDENCY_ROOT:-}/lib" \
        "${SRCROOT:-}/third_party/re2/lib" \
        /opt/homebrew/opt/re2/lib \
        /opt/homebrew/opt/abseil/lib \
        /usr/local/opt/re2/lib \
        /usr/local/opt/abseil/lib; do
        [[ -n "$root" && -f "$root/$name" ]] || continue
        printf '%s\n' "$root/$name"
        return 0
      done
      ;;
  esac

  return 1
}

has_copied_name() {
  local name="$1"
  [[ "$COPIED_NAMES" == *"
$name
"* ]]
}

run_install_name_tool() {
  local diagnostics
  diagnostics="$(mktemp "${TMPDIR:-/tmp}/mace-install-name.XXXXXX")"
  if ! install_name_tool "$@" 2>"$diagnostics"; then
    cat "$diagnostics" >&2
    rm -f "$diagnostics"
    return 1
  fi
  # A valid existing signature is expected to be invalidated here; every
  # modified image is re-signed before this script returns.
  rm -f "$diagnostics"
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

    local target="$FRAMEWORKS_DIR/$name"
    if [[ ! -e "$target" || "$(realpath "$dep")" != "$(realpath "$target")" ]]; then
      cp -fL "$dep" "$target"
    fi
    chmod u+w "$FRAMEWORKS_DIR/$name"

    while IFS= read -r child; do
      if resolved="$(resolve_embeddable_dependency "$child")"; then
        QUEUE+=("$resolved")
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
      run_install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$path"
    fi
  done < <(otool -L "$path" | awk 'NR > 1 { print $1 }')
}

rewrite_dylib_references() {
  local dylib
  for dylib in "$FRAMEWORKS_DIR"/*.dylib "$APP_PATH/Contents/MacOS"/*.dylib; do
    [[ -f "$dylib" ]] || continue
    # install_name_tool can update a signed Mach-O and invalidates its existing
    # signature. Do not remove that signature first: some Homebrew arm64 dylibs
    # keep the signature inside __LINKEDIT, and deleting it before mutation can
    # leave a layout that install_name_tool refuses to process. Re-sign below.
    if [[ "$dylib" == "$FRAMEWORKS_DIR/"* ]]; then
      run_install_name_tool -id "@rpath/$(basename "$dylib")" "$dylib"
    fi

    while IFS= read -r dep; do
      if is_embeddable_dependency "$dep"; then
        run_install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$dylib"
      fi
    done < <(otool -L "$dylib" | awk 'NR > 1 { print $1 }')

    codesign --force --sign - "$dylib" >/dev/null 2>&1
  done
}

copy_dependency_closure
for binary in "${BINARIES[@]}"; do
  rewrite_binary_references "$binary"
done
rewrite_dylib_references
for binary in "${BINARIES[@]}"; do
  path="$APP_PATH/Contents/MacOS/$binary"
  [[ -f "$path" ]] || continue
  codesign --remove-signature "$path" >/dev/null 2>&1 || true
  codesign --force --sign - "$path" >/dev/null 2>&1
done

if [[ -n "${SCRIPT_OUTPUT_FILE_0:-}" ]]; then
  mkdir -p "$(dirname "$SCRIPT_OUTPUT_FILE_0")"
  touch "$SCRIPT_OUTPUT_FILE_0"
fi

echo "Embedded runtime dylibs in $FRAMEWORKS_DIR:"
find "$FRAMEWORKS_DIR" -maxdepth 1 -name '*.dylib' -print | sort
