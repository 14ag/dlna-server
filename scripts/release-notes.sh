#!/usr/bin/env bash
# prints release notes for a tag to stdout
# falls back to the commit list when gemini is unavailable
set -euo pipefail

tag="${1:?Usage: release-notes.sh <tag>}"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prev="$(git -C "$repo" describe --tags --abbrev=0 "${tag}^" 2>/dev/null || true)"
commits="$(git -C "$repo" log --oneline "${prev:+$prev..}$tag")"

fallback() { printf '## Changes\n\n%s\n' "${commits:-No commits found.}"; }

[ -n "${GEMINI_API_KEY:-}" ] || { fallback; exit 0; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
bash "$repo/scripts/release-diff.sh" "$prev" "$tag" "$work/diff.txt"

cat > "$work/prompt.txt" <<EOF
You are a technical writer. Given the following git commit log and source diff for release $tag of DLNA Server (a C++ UPnP/DLNA media server), write concise GitHub release notes in markdown.
Rules:
- Group changes under headings: ## What's New, ## Bug Fixes, ## Improvements
- Use bullet points
- Do not include the commit hashes
Commit log:
$commits

Source diff:
EOF
# diff capped at 400000 bytes raise the cap if notes miss detail
head -c 400000 "$work/diff.txt" >> "$work/prompt.txt"

python3 -c 'import json, pathlib, sys; print(json.dumps({"model": "gemini-3.8-flash", "input": pathlib.Path(sys.argv[1]).read_text()}))' "$work/prompt.txt" > "$work/request.json"
http_status="$(curl -sS --max-time 120 -X POST \
    "https://generativelanguage.googleapis.com/v1beta/interactions" \
    -H "x-goog-api-key: $GEMINI_API_KEY" \
    -H "Content-Type: application/json" \
    --data-binary @"$work/request.json" \
    --output "$work/response.json" \
    --write-out '%{http_code}')" || {
    printf 'Gemini request failed at transport level.\n' >&2
    exit 1
}
if [ "$http_status" -lt 200 ] || [ "$http_status" -ge 300 ]; then
    printf 'Gemini API returned HTTP %s: ' "$http_status" >&2
    python3 -c '
import json
import pathlib
import sys

try:
    response = json.loads(pathlib.Path(sys.argv[1]).read_text())
except (json.JSONDecodeError, OSError):
    print("response body was not valid JSON")
    sys.exit(0)
error = response.get("error", {}) if isinstance(response, dict) else {}
print(error.get("message", "no error message in response"))
' "$work/response.json" >&2
    exit 1
fi

notes="$(python3 -c '
import json
import pathlib
import sys

try:
    response = json.loads(pathlib.Path(sys.argv[1]).read_text())
except (json.JSONDecodeError, OSError) as error:
    print(f"Gemini returned an invalid response: {error}", file=sys.stderr)
    sys.exit(1)

if not isinstance(response, dict):
    print("Gemini response was not a JSON object.", file=sys.stderr)
    sys.exit(1)
output = response.get("output")
if isinstance(output, list):
    for item in output:
        if isinstance(item, dict) and item.get("type") == "text" and isinstance(item.get("text"), str) and item["text"]:
            print(item["text"])
            sys.exit(0)
print("Gemini response did not contain generated text.", file=sys.stderr)
sys.exit(1)
' "$work/response.json")"

printf '%s\n' "$notes"
