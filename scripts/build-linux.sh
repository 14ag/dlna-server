#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version=$(grep -E '^project\(dlna-server VERSION ' "$repo_root/CMakeLists.txt" | sed -E 's/.*VERSION ([0-9.]+).*/\1/')
output_dir="$repo_root/output/linux"
build_dir="$repo_root/build-release-linux"
release_stage_dir="$repo_root/build-release-linux-stage"
install_dir="$release_stage_dir/install"
appdir="$release_stage_dir/dlna-server.AppDir"
tools_dir="$repo_root/build-release-tools/linux"

linuxdeploy_version="1-alpha-20251107-1"
linuxdeploy_sha256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"

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

sha256_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        echo "sha256sum or shasum is required" >&2
        return 1
    fi
}

download_verified() {
    local url=$1
    local path=$2
    local expected=$3
    if [ -s "$path" ] && [ "$(sha256_file "$path")" = "$expected" ]; then
        return 0
    fi
    rm -f "$path"
    if command -v curl >/dev/null 2>&1; then
        curl -L -o "$path" "$url"
    elif command -v wget >/dev/null 2>&1; then
        wget -O "$path" "$url"
    else
        echo "curl or wget is required" >&2
        return 1
    fi
    if [ "$(sha256_file "$path")" != "$expected" ]; then
        rm -f "$path"
        echo "Checksum mismatch for $path" >&2
        return 1
    fi
}

rm -rf "$release_stage_dir" "$build_dir"
mkdir -p "$output_dir" "$tools_dir" "$release_stage_dir"

# Check & Install Dependencies
_pkgs=(
    build-essential cmake pkg-config git
    libcurl4-openssl-dev
    libgtk-4-dev
    libx11-dev libxext-dev
    libpng-dev libjpeg-dev zlib1g-dev
    dpkg-dev desktop-file-utils
    flatpak flatpak-builder
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

# Flatpak setup
sudo_run flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
if ! flatpak info org.freedesktop.Platform//24.08 >/dev/null 2>&1; then
    sudo_run flatpak install -y flathub org.freedesktop.Platform//24.08 org.freedesktop.Sdk//24.08
fi

cmake -S "$repo_root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$install_dir" -DDLNA_ENABLE_GTK4_GUI=ON
cmake --build "$build_dir" --parallel 2

# Deb package build via cpack
cpack --config "$build_dir/CPackConfig.cmake" -B "$output_dir"
rm -rf "$output_dir/_CPack_Packages"

cmake --install "$build_dir"

# AppImage build
rm -rf "$appdir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share"
cp -a "$install_dir/bin/." "$appdir/usr/bin/"
cp -a "$install_dir/share/." "$appdir/usr/share/"
cp "$repo_root/packaging/linux/AppRun" "$appdir/AppRun"
tr -d '\r' < "$repo_root/packaging/linux/dlna-server.appimage.desktop" > "$appdir/dlna-server.desktop"
tr -d '\r' < "$repo_root/packaging/linux/dlna-server.appimage.desktop" > "$appdir/usr/share/applications/dlna-server.desktop"
cp "$repo_root/resources/dlna-server.svg" "$appdir/dlna-server.svg"
chmod +x "$appdir/AppRun" "$appdir/usr/bin/dlna-server" "$appdir/usr/bin/dlna-server-gui" "$appdir/usr/bin/dlna-server-gui-bin"

linuxdeploy="$tools_dir/linuxdeploy-x86_64.AppImage"
if [ ! -s "$linuxdeploy" ]; then
    download_verified "https://github.com/linuxdeploy/linuxdeploy/releases/download/$linuxdeploy_version/linuxdeploy-x86_64.AppImage" "$linuxdeploy" "$linuxdeploy_sha256"
fi
chmod +x "$linuxdeploy"

find "$output_dir" -maxdepth 1 -type f -name '*.AppImage' -delete
(cd "$output_dir" && APPIMAGE_EXTRACT_AND_RUN=1 "$linuxdeploy" --appdir "$appdir" --desktop-file "$appdir/dlna-server.desktop" --icon-file "$appdir/dlna-server.svg" --output appimage)
appimage=$(find "$output_dir" -maxdepth 1 -type f -name '*.AppImage' | head -n 1)
mv "$appimage" "$output_dir/DLNA_Server-${version}-x86_64.AppImage"

# Flatpak build
flatpak_repo="$release_stage_dir/flatpak-repo"
flatpak_build="$release_stage_dir/flatpak-build"
flatpak_bundle="$output_dir/dlna-server-${version}-linux-x86_64.flatpak"
rm -rf "$flatpak_build" "$flatpak_repo" "$flatpak_bundle"
flatpak-builder --force-clean --repo="$flatpak_repo" "$flatpak_build" "$repo_root/packaging/flatpak/com.github.14ag.dlna_server.yml"
flatpak build-bundle "$flatpak_repo" "$flatpak_bundle" com.github.14ag.dlna_server stable

echo "Linux assets created in $output_dir"
