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
        printf '%s\n' "$DLNA_SUDO_PASSWORD" | sudo -S -p ' ' "$@"
        return
    fi

    sudo -n "$@"
}

stop_running_instances() {
    local name
    for name in dlna-server dlna-server-gui dlna-server-gui-bin; do
        if pgrep -x "$name" >/dev/null 2>&1; then
            sudo_run pkill -TERM -x "$name" || true
        fi
    done
}

stop_running_instances
sudo_run rm -f /usr/bin/dlna-server
sudo_run rm -f /usr/bin/dlna-server-gui
sudo_run rm -f /usr/bin/dlna-server-gui-bin

if [ -z "$package_path" ]; then
    package_path=$(
        find "$output_dir" -maxdepth 1 -type f -name 'dlna-server_*.deb' -printf '%T@ %p\n' |
            sort -nr |
            awk 'NR==1 { $1=""; sub(/^ /, ""); print; exit }'
    )
fi

if [ -z "$package_path" ] || [ ! -f "$package_path" ]; then
    # No Debian package was produced (--no-deb or packaging skipped in
    # constrained environments). Fall back to the binaries already installed
    # to /usr/local by build-linux-assets.sh in install-only mode, mirroring
    # them into the well-known /usr/bin paths.
    if [ -x /usr/local/bin/dlna-server-gui-bin ] && [ -x /usr/local/bin/dlna-server ]; then
        sudo_run cp /usr/local/bin/dlna-server /usr/bin/dlna-server
        sudo_run cp /usr/local/bin/dlna-server-gui-bin /usr/bin/dlna-server-gui-bin
        if [ -e /usr/local/bin/dlna-server-gui ]; then
            sudo_run cp /usr/local/bin/dlna-server-gui /usr/bin/dlna-server-gui
        else
            sudo_run sh -c 'printf '\''#!/usr/bin/env bash\nFLTK_BACKEND=x11 exec /usr/bin/dlna-server-gui-bin "$@"\n'\'' > /usr/bin/dlna-server-gui && chmod +x /usr/bin/dlna-server-gui'
        fi
        echo "Installed (from /usr/local, no .deb): /usr/bin/dlna-server /usr/bin/dlna-server-gui-bin"
        exit 0
    fi

    echo "No built Debian package found in $output_dir and no /usr/local binaries to fall back to" >&2
    exit 1
fi

if ! command -v dpkg >/dev/null 2>&1; then
    echo "dpkg not found" >&2
    exit 1
fi

# A fresh .deb is available, so clean out any previously installed binaries
# (both /usr/local from make install and the old /usr/bin package) before
# installing the new archive, keeping the layout consistent.
sudo_run rm -f /usr/local/bin/dlna-server
sudo_run rm -f /usr/local/bin/dlna-server-gui
sudo_run rm -f /usr/local/bin/dlna-server-gui-bin
sudo_run rm -rf /usr/local/share/dlna-server
sudo_run dpkg -P dlna-server || true
sudo_run dpkg -i "$package_path"
# Keep the documented WSLg/Windows shortcut target stable.  Debian installs
# binaries below /usr/bin, while existing WSL shortcuts invoke /usr/local/bin.
sudo_run ln -sfn /usr/bin/dlna-server-gui /usr/local/bin/dlna-server-gui
sudo_run ln -sfn /usr/bin/dlna-server-gui-bin /usr/local/bin/dlna-server-gui-bin
sudo_run ln -sfn /usr/bin/dlna-server /usr/local/bin/dlna-server
echo "Installed: $package_path"
