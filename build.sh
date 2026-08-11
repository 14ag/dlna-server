#!/usr/bin/env bash
# build.sh — Linux orchestrator entry point
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

INSTALL=0
RELEASE=0
REL_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --install)
            INSTALL=1
            shift
            ;;
        --release)
            RELEASE=1
            shift
            ;;
        --notes)
            REL_ARGS+=("--notes")
            shift
            ;;
        --update=*)
            REL_ARGS+=("$1")
            shift
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

if [ "$RELEASE" = "1" ]; then
    echo "Releasing Linux assets..."
    bash "$script_dir/scripts/release-linux.sh" "${REL_ARGS[@]:+${REL_ARGS[@]}}"
    exit 0
fi

if [ "$INSTALL" = "1" ]; then
    echo "Building and installing Linux application..."
    bash "$script_dir/scripts/build-linux.sh"
    bash "$script_dir/scripts/install-dlna-server-linux.sh"
    exit 0
fi

# Default: Build Linux assets
echo "Building Linux assets..."
bash "$script_dir/scripts/build-linux.sh"
