#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
echo "App-bundled dylibs are no longer reused; building static dependencies instead." >&2
exec "$root_dir/scripts/prepare-re2-deps.sh" "${2:-$root_dir/third_party/re2}"
