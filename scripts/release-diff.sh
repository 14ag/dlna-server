#!/usr/bin/env bash
set -euo pipefail
# Src-only diff for release notes. Usage: release-diff.sh <prev_tag> <tag> [out_file]
prev="${1:-}"
tag="${2:?Usage: release-diff.sh <prev_tag> <tag> [out_file]}"
out="${3:-/dev/stdout}"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$script_dir/.." && pwd)"
if [ -z "$prev" ]; then printf 'No previous tag; src diff skipped.\n' > "$out"; exit 0; fi
git -C "$repo" diff "${prev}..${tag}" -- src/ ":(exclude)*.svg" > "$out"
