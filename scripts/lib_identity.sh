#!/usr/bin/env bash

_identity_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# strip carriage returns before sourcing
source <(tr -d '\r' < "$_identity_root/identity.env")
DLNA_GTK_APP_ID="${DLNA_REVERSE_DNS_PREFIX}.${DLNA_APP_ID}"
export DLNA_APP_ID DLNA_REVERSE_DNS_PREFIX DLNA_GTK_APP_ID DLNA_PRODUCT_NAME

# prints a template with every identity placeholder filled in
stamp_identity() {
    sed -e "s|@DLNA_GTK_APP_ID@|$DLNA_GTK_APP_ID|g" \
        -e "s|@DLNA_APP_ID@|$DLNA_APP_ID|g" \
        -e "s|@DLNA_PRODUCT_NAME@|$DLNA_PRODUCT_NAME|g" "$1"
}

resolve_version() {
    local lib_dir
    lib_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    if [ -z "${DLNA_VERSION_TAG:-}" ]; then
        DLNA_VERSION_TAG="$(python3 "$lib_dir/version.py")"
    fi
    DLNA_VERSION="$(python3 "$lib_dir/version.py" --numeric-of "$DLNA_VERSION_TAG")"
    export DLNA_VERSION_TAG DLNA_VERSION
}
