#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="${1:-$root_dir/third_party/re2}"

mkdir -p "$dest/include" "$dest/lib"
rm -rf "$dest/include/re2" "$dest/include/absl"
rm -f "$dest/lib"/libre2*.dylib "$dest/lib"/libabsl*.dylib

copy_header_dir() {
  local header_dir="$1"
  [[ -d "$header_dir" ]] || return 1
  cp -a "$header_dir" "$dest/include/"
}

copy_matching_libs() {
  local lib_root="$1"
  local pattern="$2"
  compgen -G "$lib_root/$pattern" >/dev/null || return 1
  cp -a "$lib_root"/$pattern "$dest/lib/"
}

copied_re2_headers=0
copied_absl_headers=0
copied_re2_libs=0
copied_absl_libs=0

for prefix in \
  "${RE2_PREFIX:-}" \
  /opt/homebrew/opt/re2 \
  /usr/local/opt/re2 \
  /opt/local; do
  [[ -n "${prefix:-}" ]] || continue
  if copy_header_dir "$prefix/include/re2"; then
    copied_re2_headers=1
    break
  fi
done

for source_root in \
  "${RE2_SOURCE_DIR:-}" \
  "$root_dir/artifacts/re2-source" \
  /tmp/re2-main; do
  [[ $copied_re2_headers -eq 0 ]] || break
  [[ -n "${source_root:-}" ]] || continue
  if copy_header_dir "$source_root/re2"; then
    copied_re2_headers=1
  fi
done

for prefix in \
  "${ABSEIL_PREFIX:-}" \
  "${RE2_PREFIX:-}" \
  /opt/homebrew/opt/abseil \
  /usr/local/opt/abseil \
  /opt/homebrew/opt/re2 \
  /usr/local/opt/re2 \
  /opt/local; do
  [[ -n "${prefix:-}" ]] || continue
  if copy_header_dir "$prefix/include/absl"; then
    copied_absl_headers=1
    break
  fi
done

for source_root in \
  "${ABSEIL_SOURCE_DIR:-}" \
  "$root_dir/artifacts/abseil-source" \
  /tmp/abseil-master; do
  [[ $copied_absl_headers -eq 0 ]] || break
  [[ -n "${source_root:-}" ]] || continue
  if copy_header_dir "$source_root/absl"; then
    copied_absl_headers=1
  fi
done

for prefix in \
  "${RE2_PREFIX:-}" \
  /opt/homebrew/opt/re2 \
  /usr/local/opt/re2 \
  /opt/local; do
  [[ -n "${prefix:-}" ]] || continue
  if copy_matching_libs "$prefix/lib" "libre2*.dylib"; then
    copied_re2_libs=1
    break
  fi
done

for lib_root in \
  "${RE2_LIB_DIR:-}" \
  "$root_dir/artifacts/MacEverything-macOS/build/Release/MacEverything.app/Contents/Frameworks"; do
  [[ $copied_re2_libs -eq 0 ]] || break
  [[ -n "${lib_root:-}" ]] || continue
  if copy_matching_libs "$lib_root" "libre2*.dylib"; then
    copied_re2_libs=1
  fi
done

for prefix in \
  "${ABSEIL_PREFIX:-}" \
  "${RE2_PREFIX:-}" \
  /opt/homebrew/opt/abseil \
  /usr/local/opt/abseil \
  /opt/homebrew/opt/re2 \
  /usr/local/opt/re2 \
  /opt/local; do
  [[ -n "${prefix:-}" ]] || continue
  if copy_matching_libs "$prefix/lib" "libabsl*.dylib"; then
    copied_absl_libs=1
    break
  fi
done

for lib_root in \
  "${ABSEIL_LIB_DIR:-}" \
  "${RE2_LIB_DIR:-}" \
  "$root_dir/artifacts/MacEverything-macOS/build/Release/MacEverything.app/Contents/Frameworks"; do
  [[ $copied_absl_libs -eq 0 ]] || break
  [[ -n "${lib_root:-}" ]] || continue
  if copy_matching_libs "$lib_root" "libabsl*.dylib"; then
    copied_absl_libs=1
  fi
done

if [[ $copied_re2_headers -ne 1 ]]; then
  echo "error: RE2 headers not found. Provide RE2_PREFIX/RE2_SOURCE_DIR or install re2." >&2
  exit 69
fi

if [[ $copied_absl_headers -ne 1 ]]; then
  echo "error: Abseil headers not found. Provide ABSEIL_PREFIX/ABSEIL_SOURCE_DIR or install abseil." >&2
  exit 69
fi

if [[ $copied_re2_libs -ne 1 ]]; then
  echo "error: libre2 dylib not found. Provide RE2_PREFIX/RE2_LIB_DIR or install re2." >&2
  exit 69
fi

if [[ $copied_absl_libs -ne 1 ]]; then
  echo "error: libabsl dylibs not found. Provide ABSEIL_PREFIX/ABSEIL_LIB_DIR or install abseil." >&2
  exit 69
fi

echo "Prepared RE2 dependencies in $dest"
