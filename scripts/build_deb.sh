#!/usr/bin/env bash
# build_deb.sh — Build Debian package (.deb) using CPack into output/linux
set -euo pipefail

export PATH="/usr/bin:/bin:/usr/sbin:/sbin"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output_dir="$repo_root/output/linux"

build_root=$(mktemp -d "${TMPDIR:-/tmp}/dlna-server-deb-build.XXXXXX")
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
        if printf '%s\n' "$DLNA_SUDO_PASSWORD" | sudo -S -p '' "$@" 2>/dev/null; then
            return 0
        fi
    fi
    if sudo -n "$@" 2>/dev/null; then
        return 0
    fi
    "$@"
}

mkdir -p "$output_dir" "$release_stage_dir"

# Check & Install Debian packaging prerequisites
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
    echo "[INFO] Installing build prerequisites for deb package..."
    sudo_run env DEBIAN_FRONTEND=noninteractive apt-get update -qq
    sudo_run env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${_pkgs[@]}"
fi

echo "[INFO] Configuring and building deb package..."
cmake -S "$repo_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$install_dir" \
    -DDLNA_ENABLE_GTK4_GUI=ON \
    -DCMAKE_C_COMPILER=/usr/bin/gcc \
    -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
    -DCMAKE_LINKER=/usr/bin/ld
cmake --build "$build_dir" --parallel 2

# Generate Debian package via CPack
cpack --config "$build_dir/CPackConfig.cmake" -B "$output_dir"
sudo_run rm -rf "$output_dir/_CPack_Packages" 2>/dev/null || true
sudo_run chown -R "$(id -u):$(id -g)" "$output_dir" 2>/dev/null || true

deb_file=$(find "$output_dir" -maxdepth 1 -type f -name 'dlna-server_*.deb' | head -n 1)
if [ -z "$deb_file" ]; then
    echo "[ERROR] Debian package generation failed." >&2
    exit 1
fi

echo "Debian package created: $deb_file"
