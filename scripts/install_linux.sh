#!/usr/bin/env bash
# install_linux.sh — Remove existing installation and install dlna-server package/binaries
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

stop_running_instances() {
    local name
    for name in dlna-server dlna-server-gui dlna-server-gui-bin; do
        if pgrep -x "$name" >/dev/null 2>&1; then
            sudo_run pkill -TERM -x "$name" || true
        fi
    done
}

# Stop running instances first
stop_running_instances

# Remove existing installation as requested: "first runs sudo apt remove dlna-server"
echo "[INFO] Removing existing dlna-server installation..."
sudo_run env DEBIAN_FRONTEND=noninteractive apt-get remove -y dlna-server || true
sudo_run dpkg -P dlna-server >/dev/null 2>&1 || true

# Clean legacy manual installation paths
sudo_run rm -f /usr/bin/dlna-server
sudo_run rm -f /usr/bin/dlna-server-gui
sudo_run rm -f /usr/bin/dlna-server-gui-bin
sudo_run rm -f /usr/local/bin/dlna-server
sudo_run rm -f /usr/local/bin/dlna-server-gui
sudo_run rm -f /usr/local/bin/dlna-server-gui-bin
sudo_run rm -rf /usr/local/share/dlna-server

# Find package if not explicitly provided
if [ -z "$package_path" ]; then
    package_path=$(
        find "$output_dir" -maxdepth 1 -type f -name 'dlna-server_*.deb' -printf '%T@ %p\n' 2>/dev/null |
            sort -nr |
            awk 'NR==1 { $1=""; sub(/^ /, ""); print; exit }'
    )
fi

# Install via Debian package if found
if [ -n "$package_path" ] && [ -f "$package_path" ]; then
    echo "[INFO] Installing Debian package: $package_path"
    sudo_run dpkg -i "$package_path"
    # Keep documented WSLg/Windows shortcut targets pointing to /usr/bin binaries
    sudo_run ln -sfn /usr/bin/dlna-server-gui /usr/local/bin/dlna-server-gui
    sudo_run ln -sfn /usr/bin/dlna-server-gui-bin /usr/local/bin/dlna-server-gui-bin
    sudo_run ln -sfn /usr/bin/dlna-server /usr/local/bin/dlna-server
    echo "Installed: $package_path"
    exit 0
fi

# Fallback: Install directly from built assets in output/linux
if [ -x "$output_dir/dlna-server" ] && [ -x "$output_dir/dlna-server-gui-bin" ]; then
    echo "[INFO] Installing direct binaries from $output_dir to /usr/bin..."
    sudo_run cp -p "$output_dir/dlna-server" /usr/bin/dlna-server
    sudo_run cp -p "$output_dir/dlna-server-gui-bin" /usr/bin/dlna-server-gui-bin
    if [ -e "$output_dir/dlna-server-gui" ]; then
        sudo_run cp -p "$output_dir/dlna-server-gui" /usr/bin/dlna-server-gui
    fi
    if [ -d "$output_dir/share" ]; then
        sudo_run cp -a "$output_dir/share/." /usr/share/
    fi
    sudo_run ln -sfn /usr/bin/dlna-server-gui /usr/local/bin/dlna-server-gui
    sudo_run ln -sfn /usr/bin/dlna-server-gui-bin /usr/local/bin/dlna-server-gui-bin
    sudo_run ln -sfn /usr/bin/dlna-server /usr/local/bin/dlna-server
    echo "Installed binaries to /usr/bin and symlinked to /usr/local/bin"
    exit 0
fi

echo "[ERROR] No built Debian package or binary assets found to install." >&2
exit 1

