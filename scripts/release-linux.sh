#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$script_dir/.." && pwd)"
output_dir="$repo/output"

NOTES=0
UPDATE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --notes) NOTES=1; shift ;;
        --update=*) UPDATE="${1#*=}"; shift ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if ! command -v gh >/dev/null 2>&1; then
    echo "gh CLI not found." >&2
    exit 1
fi

TAG="$UPDATE"
if [ -z "$TAG" ]; then
    TAG="${GITHUB_REF_NAME:-}"
    if [ -z "$TAG" ]; then
        TAG=$(git -C "$repo" describe --tags --exact-match HEAD 2>/dev/null || true)
    fi
    if [ -z "$TAG" ]; then
        echo "No tag found. Pass --update=[tag] or set GITHUB_REF_NAME." >&2
        exit 1
    fi
fi

RELEASE_NOTES="Release assets build."
if [ "$NOTES" = "1" ]; then
    if [ -z "${GEMINI_API_KEY:-}" ]; then
        echo "GEMINI_API_KEY environment variable is not set." >&2
        exit 1
    fi
    
    PREV_TAG=$(git -C "$repo" describe --tags --abbrev=0 "${TAG}^" 2>/dev/null || true)
    if [ -z "$PREV_TAG" ]; then
        COMMITS=$(git -C "$repo" log --oneline)
    else
        COMMITS=$(git -C "$repo" log --oneline "${PREV_TAG}..${TAG}")
    fi
    [ -z "$COMMITS" ] && COMMITS="No commits found."
    
    PROMPT="You are a technical writer. Given the following git commit log for release ${TAG} of DLNA Server (a C++ UPnP/DLNA media server), write concise GitHub release notes in markdown.
Rules:
- Group changes under headings: ## What's New, ## Bug Fixes, ## Improvements
- Use bullet points
- Do not include the commit hashes
Commit log:
${COMMITS}"

    REQUEST_BODY=$(jq -n --arg prompt "$PROMPT" '{contents:[{parts:[{text:$prompt}]}]}')
    RESPONSE=$(curl -sf -X POST \
        "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=${GEMINI_API_KEY}" \
        -H "Content-Type: application/json" \
        -d "$REQUEST_BODY" 2>/dev/null || true)
    
    RELEASE_NOTES=$(printf '%s' "$RESPONSE" | jq -r '.candidates[0].content.parts[0].text // empty' 2>/dev/null || true)
    if [ -z "$RELEASE_NOTES" ]; then
        RELEASE_NOTES="## Changes

${COMMITS}"
    fi
fi

ASSETS=()
while IFS= read -r -d '' f; do
    ASSETS+=("$f")
done < <(find "$output_dir" -type f \( -name "*.zip" -o -name "*.deb" -o -name "*.AppImage" -o -name "*.flatpak" \) -print0)

if [ "${#ASSETS[@]}" -eq 0 ]; then
    echo "No release assets found." >&2
    exit 1
fi

if [ -n "$UPDATE" ]; then
    echo "Updating release $TAG with assets..."
    for asset in "${ASSETS[@]}"; do
        gh release upload "$TAG" "$asset" --clobber
    done
else
    echo "Creating release $TAG..."
    notes_file=$(mktemp)
    printf '%s' "$RELEASE_NOTES" > "$notes_file"
    gh release create "$TAG" --title "$TAG" --notes-file "$notes_file" "${ASSETS[@]}"
    rm -f "$notes_file"
fi
