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

notes="$(jq -n --rawfile p "$work/prompt.txt" '{contents:[{parts:[{text:$p}]}]}' \
    | curl -sf --max-time 120 -X POST \
        "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent" \
        -H "x-goog-api-key: $GEMINI_API_KEY" \
        -H "Content-Type: application/json" \
        --data-binary @- \
    | jq -r '.candidates[0].content.parts[0].text // empty' || true)"

if [ -n "$notes" ]; then printf '%s\n' "$notes"; else fallback; fi
