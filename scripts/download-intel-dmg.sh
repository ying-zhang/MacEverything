#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
artifact_dir="${ARTIFACT_DIR:-$root_dir/artifacts}"
workflow="${WORKFLOW:-build-macos.yml}"
branch="${BRANCH:-main}"
artifact_name="${ARTIFACT_NAME:-MacEverything-macOS-x86_64}"

mkdir -p "$artifact_dir"

run_id="${1:-}"
if [[ -z "$run_id" ]]; then
  run_id="$(gh run list \
    --workflow "$workflow" \
    --branch "$branch" \
    --status success \
    --limit 1 \
    --json databaseId \
    --jq '.[0].databaseId')"
fi

if [[ -z "$run_id" || "$run_id" == "null" ]]; then
  echo "error: no successful $workflow run found on branch $branch" >&2
  exit 69
fi

tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/maceverything-intel-dmg.XXXXXX")"
cleanup() {
  rm -rf "$tmp_dir"
}
trap cleanup EXIT

gh run download "$run_id" \
  --name "$artifact_name" \
  --dir "$tmp_dir"

dmg_path="$(find "$tmp_dir" -name 'MacEverything-x86_64.dmg' -print -quit)"
if [[ -z "$dmg_path" ]]; then
  echo "error: MacEverything-x86_64.dmg not found in downloaded artifact" >&2
  exit 66
fi

cp -f "$dmg_path" "$artifact_dir/MacEverything-x86_64.dmg"
echo "Downloaded $artifact_dir/MacEverything-x86_64.dmg from run $run_id"
