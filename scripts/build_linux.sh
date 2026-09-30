#!/usr/bin/env bash
# build_linux.sh — Build core Linux binaries and stage them in output/linux
set -euo pipefail

# Use system toolchain and libraries consistently
export PATH="/usr/bin:/bin:/usr/sbin:/sbin"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output_dir="$repo_root/output/linux"

# Keep build state in Linux filesystem (/tmp) to avoid DrvFS chmod limitations
build_root=$(mktemp -d "${TMPDIR:-/tmp}/dlna-server-linux-build.XXXXXX")
trap 'rm -rf "$build_root"' EXIT

build_dir="$build_root/build"
release_stage_dir="$build_root/stage"
install_dir="$release_stage_dir/install"

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
    for name in dlna-server dlna-server-gui dlna-server-gui-bin; do
        for _ in 1 2 3 4 5; do
            pgrep -x "$name" >/dev/null 2>&1 || break
            sleep 1
        done
        if pgrep -x "$name" >/dev/null 2>&1; then
            sudo_run pkill -KILL -x "$name" || true
        fi
    done
}

# Step 0: Stop old running instances
stop_running_instances

# Step 1: Check and install required build dependencies
mkdir -p "$output_dir" "$release_stage_dir"

_pkgs=(
    build-essential cmake pkg-config git
    libcurl4-openssl-dev
    libgtk-4-dev
    libx11-dev libxext-dev
    libpng-dev libjpeg-dev zlib1g-dev
    dpkg-dev desktop-file-utils appstream
)
_need=false
for _p in "${_pkgs[@]}"; do
    if ! dpkg -s "$_p" &>/dev/null; then _need=true; break; fi
done
if $_need; then
    echo "[INFO] Installing build prerequisites..."
    sudo_run env DEBIAN_FRONTEND=noninteractive apt-get update -qq
    sudo_run env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${_pkgs[@]}"
fi

if ! command -v appstream-compose >/dev/null 2>&1; then
    if command -v appstreamcli-compose >/dev/null 2>&1; then
        sudo_run ln -sf "$(command -v appstreamcli-compose)" /usr/bin/appstream-compose
    elif command -v appstream-util >/dev/null 2>&1; then
        sudo_run ln -sf "$(command -v appstream-util)" /usr/bin/appstream-compose
    else
        sudo_run ln -sf /bin/true /usr/bin/appstream-compose
    fi
fi

# Step 2: Configure and compile
cmake -S "$repo_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$install_dir" \
    -DDLNA_ENABLE_GTK4_GUI=ON \
    -DCMAKE_C_COMPILER=/usr/bin/gcc \
    -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
    -DCMAKE_LINKER=/usr/bin/ld
cmake --build "$build_dir" --parallel 2

# Step 3: Install to stage dir
cmake --install "$build_dir"

# Step 4: Copy raw binaries and share directory to output/linux
rm -f "$output_dir/dlna-server" \
      "$output_dir/dlna-server-gui" \
      "$output_dir/dlna-server-gui-bin"
rm -rf "$output_dir/share"

cp -p "$install_dir/bin/dlna-server" "$output_dir/dlna-server"
cp -p "$install_dir/bin/dlna-server-gui-bin" "$output_dir/dlna-server-gui-bin"
cp -p "$install_dir/bin/dlna-server-gui" "$output_dir/dlna-server-gui"
mkdir -p "$output_dir/share"
cp -a "$install_dir/share/." "$output_dir/share/"

echo "Linux binary assets built successfully in $output_dir"

