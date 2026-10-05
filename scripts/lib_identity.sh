#!/usr/bin/env bash

DLNA_APP_ID="com.github.dlna-server-14ag"

resolve_version() {
    local lib_dir
    lib_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    if [ -z "${DLNA_VERSION_TAG:-}" ]; then
        DLNA_VERSION_TAG="$(python3 "$lib_dir/version.py")"
    fi
    DLNA_VERSION="$(python3 "$lib_dir/version.py" --numeric-of "$DLNA_VERSION_TAG")"
    export DLNA_VERSION_TAG DLNA_VERSION
}
