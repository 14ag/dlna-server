#!/usr/bin/env bash
set -euo pipefail

repo_root=${DLNA_REPO_ROOT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)}
output_dir=${DLNA_OUTPUT_DIR:-"$repo_root/output/linux"}
package_path=${DLNA_POSIX_DEB:-}

case "$output_dir" in /*) ;; *) output_dir="$repo_root/$output_dir" ;; esac
if [ -n "$package_path" ]; then
    case "$package_path" in /*) ;; *) package_path="$repo_root/$package_path" ;; esac
fi

sudo_run() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
        return
    fi

    if [ -n "${DLNA_SUDO_PASSWORD:-}" ]; then
        printf '%s\n' "$DLNA_SUDO_PASSWORD" | sudo -S -p '' "$@"
        return
    fi

    sudo -n "$@"
}

if [ -z "$package_path" ]; then
    package_path=$(
        find "$output_dir" -maxdepth 1 -type f -name 'dlna-server_*.deb' -printf '%T@ %p\n' |
            sort -nr |
            awk 'NR==1 { $1=""; sub(/^ /, ""); print; exit }'
    )
fi

if [ -z "$package_path" ] || [ ! -f "$package_path" ]; then
    echo "No built Debian package found in $output_dir" >&2
    exit 1
fi

if ! command -v dpkg >/dev/null 2>&1; then
    echo "dpkg not found" >&2
    exit 1
fi

sudo_run dpkg -i "$package_path"
echo "Installed: $package_path"
