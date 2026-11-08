#!/usr/bin/env bash
# build_appimage.sh — Build .AppImage bundle in output/linux
set -euo pipefail

export PATH="/usr/bin:/bin:/usr/sbin:/sbin"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/lib_identity.sh"
for arg in "$@"; do
    case "$arg" in
        --version=*) DLNA_VERSION_TAG="${arg#*=}" ;;
        *) echo "Unknown option: $arg" >&2; exit 1 ;;
    esac
done
resolve_version
version="$DLNA_VERSION"
output_dir="$repo_root/output/linux"
tools_dir="$repo_root/build-release-tools/linux"

build_root=$(mktemp -d "${TMPDIR:-/tmp}/dlna-server-appimage-build.XXXXXX")
trap 'rm -rf "$build_root"' EXIT

appdir="$build_root/dlna-server.AppDir"

linuxdeploy_version="1-alpha-20251107-1"
linuxdeploy_sha256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"

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

mkdir -p "$output_dir" "$tools_dir"

# Ensure binary assets exist
if [ ! -f "$output_dir/dlna-server" ] || [ ! -f "$output_dir/dlna-server-gui" ] || [ ! -f "$output_dir/dlna-server-gui-bin" ]; then
    echo "[INFO] Binaries missing in $output_dir. Running build_linux.sh first..."
    bash "$repo_root/scripts/build_linux.sh"
fi

# Stage AppDir
rm -rf "$appdir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share"
cp -p "$output_dir/dlna-server" "$appdir/usr/bin/"
cp -p "$output_dir/dlna-server-gui" "$appdir/usr/bin/"
cp -p "$output_dir/dlna-server-gui-bin" "$appdir/usr/bin/"
if [ -d "$output_dir/share" ]; then
    cp -a "$output_dir/share/." "$appdir/usr/share/"
fi

cp "$repo_root/packaging/linux/AppRun" "$appdir/AppRun"
mkdir -p "$appdir/usr/share/applications"
stamp_identity "$repo_root/packaging/linux/portable.desktop.in" | tr -d '\r' > "$appdir/${DLNA_GTK_APP_ID}.desktop"
cp "$appdir/${DLNA_GTK_APP_ID}.desktop" "$appdir/usr/share/applications/${DLNA_GTK_APP_ID}.desktop"
cp "$repo_root/resources/${DLNA_APP_ID}.svg" "$appdir/${DLNA_GTK_APP_ID}.svg"
chmod +x "$appdir/AppRun" "$appdir/usr/bin/dlna-server" "$appdir/usr/bin/dlna-server-gui" "$appdir/usr/bin/dlna-server-gui-bin"

linuxdeploy="$tools_dir/linuxdeploy-x86_64.AppImage"
if [ ! -s "$linuxdeploy" ]; then
    download_verified "https://github.com/linuxdeploy/linuxdeploy/releases/download/$linuxdeploy_version/linuxdeploy-x86_64.AppImage" "$linuxdeploy" "$linuxdeploy_sha256"
fi
chmod +x "$linuxdeploy"

find "$output_dir" -maxdepth 1 -type f -name '*.AppImage' -delete
if (cd "$output_dir" && APPIMAGE_EXTRACT_AND_RUN=1 "$linuxdeploy" --appdir "$appdir" --desktop-file "$appdir/${DLNA_GTK_APP_ID}.desktop" --icon-file "$appdir/${DLNA_GTK_APP_ID}.svg" --output appimage); then
    appimage=$(find "$output_dir" -maxdepth 1 -type f -name '*.AppImage' | head -n 1)
    mv "$appimage" "$output_dir/${DLNA_PRODUCT_NAME}-${version}-x86_64.AppImage"
    echo "AppImage created: $output_dir/${DLNA_PRODUCT_NAME}-${version}-x86_64.AppImage"
else
    echo "[WARN] AppImage runtime unavailable; failed to create AppImage bundle." >&2
    exit 1
fi

