#!/usr/bin/env bash
set -euo pipefail

repo_root=${DLNA_REPO_ROOT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)}
version=${DLNA_SERVER_VERSION:-$(grep -E '^project\(dlna-server VERSION ' "$repo_root/CMakeLists.txt" | sed -E 's/.*VERSION ([0-9.]+).*/\1/')}
platform_dir=${DLNA_LINUX_PLATFORM_DIR:-"$repo_root/output/linux"}
build_dir=${DLNA_LINUX_BUILD_DIR:-"$repo_root/build-release-linux"}
release_stage_dir=${DLNA_LINUX_STAGE_DIR:-"$repo_root/build-release-linux-stage"}
install_only=${DLNA_INSTALL_ONLY:-0}
package_only=${DLNA_INSTALL_PACKAGE_ONLY:-0}
if [ "$install_only" = "1" ]; then
    install_dir=${DLNA_LINUX_INSTALL_DIR:-/usr/local}
else
    install_dir=${DLNA_LINUX_INSTALL_DIR:-"$release_stage_dir/install"}
fi
output_dir=${DLNA_OUTPUT_DIR:-"$platform_dir"}

case "$platform_dir" in /*) ;; *) platform_dir="$repo_root/$platform_dir" ;; esac
case "$build_dir" in /*) ;; *) build_dir="$repo_root/$build_dir" ;; esac
case "$release_stage_dir" in /*) ;; *) release_stage_dir="$repo_root/$release_stage_dir" ;; esac
case "$install_dir" in /*) ;; *) install_dir="$repo_root/$install_dir" ;; esac
case "$output_dir" in /*) ;; *) output_dir="$repo_root/$output_dir" ;; esac
appdir="$release_stage_dir/dlna-server.AppDir"
tools_dir=${DLNA_RELEASE_TOOLS_DIR:-"$repo_root/build-release-tools/linux"}
case "$tools_dir" in /*) ;; *) tools_dir="$repo_root/$tools_dir" ;; esac
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
        echo "sha256sum or shasum is required to verify downloaded packaging tools" >&2
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
        echo "curl or wget is required to download packaging tools" >&2
        return 1
    fi

    if [ "$(sha256_file "$path")" != "$expected" ]; then
        rm -f "$path"
        echo "Checksum mismatch for $path" >&2
        return 1
    fi
}

if [ "${DLNA_NO_CLEAN:-0}" != "1" ] && [ "$install_only" != "1" ]; then
    case "$platform_dir" in
        "$repo_root"/output/*) rm -rf "$platform_dir" ;;
        *) echo "Refusing to clean non-output platform dir: $platform_dir" >&2; exit 1 ;;
    esac
    rm -rf "$release_stage_dir"
fi
mkdir -p "$output_dir" "$tools_dir" "$release_stage_dir"

# Install build prerequisites if missing
_pkgs=(
    build-essential cmake pkg-config git
    libcurl4-openssl-dev libx11-dev libxft-dev libxext-dev
    libxinerama-dev libxcursor-dev libxrender-dev libxfixes-dev
    libpng-dev libjpeg-dev zlib1g-dev
    libgl1-mesa-dev libglu1-mesa-dev
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
unset _pkgs _need _p

cmake_args=(
    -S "$repo_root"
    -B "$build_dir"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX="$install_dir"
)

if [ "${DLNA_LINUX_GUI_BUILD:-1}" = "1" ]; then
    cmake_args+=("-DDLNA_ENABLE_FLTK_GUI=ON")
else
    cmake_args+=("-DDLNA_ENABLE_FLTK_GUI=OFF")
fi

if [ -n "${DLNA_FLTK_SOURCE_DIR:-}" ] && [ -d "$DLNA_FLTK_SOURCE_DIR" ]; then
    cmake_args+=("-DFETCHCONTENT_SOURCE_DIR_FLTK=$DLNA_FLTK_SOURCE_DIR")
elif [ -d "$repo_root/build-wsl-native/_deps/fltk-src" ]; then
    cmake_args+=("-DFETCHCONTENT_SOURCE_DIR_FLTK=$repo_root/build-wsl-native/_deps/fltk-src")
fi

cmake "${cmake_args[@]}"
cmake --build "$build_dir" --parallel "${DLNA_BUILD_JOBS:-2}"
if [ "$install_only" = "1" ]; then
    sudo_run cmake --install "$build_dir"
    echo ""
    echo "DLNA Server installed to /usr/local"
    echo "  Headless: dlna-server --source /path/to/media"
    echo "  GUI:      dlna-server-gui"
    exit 0
fi
cmake --install "$build_dir"

cpack --config "$build_dir/CPackConfig.cmake" -B "$output_dir"

if [ "$package_only" = "1" ]; then
    exit 0
fi

rm -rf "$appdir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share"
cp -a "$install_dir/bin/." "$appdir/usr/bin/"
cp -a "$install_dir/share/." "$appdir/usr/share/"
cp "$repo_root/packaging/linux/AppRun" "$appdir/AppRun"
tr -d '\r' < "$repo_root/packaging/linux/dlna-server.appimage.desktop" > "$appdir/dlna-server.desktop"
tr -d '\r' < "$repo_root/packaging/linux/dlna-server.appimage.desktop" > "$appdir/usr/share/applications/dlna-server.desktop"
cp "$repo_root/resources/dlna-server.svg" "$appdir/dlna-server.svg"
chmod +x "$appdir/AppRun" "$appdir/usr/bin/dlna-server" "$appdir/usr/bin/dlna-server-gui" "$appdir/usr/bin/dlna-server-gui-bin"

linuxdeploy=${LINUXDEPLOY:-"$tools_dir/linuxdeploy-x86_64.AppImage"}
if [ ! -s "$linuxdeploy" ]; then
    download_verified "https://github.com/linuxdeploy/linuxdeploy/releases/download/$linuxdeploy_version/linuxdeploy-x86_64.AppImage" "$linuxdeploy" "$linuxdeploy_sha256"
fi
chmod +x "$linuxdeploy"

find "$output_dir" -maxdepth 1 -type f -name '*.AppImage' -delete
(cd "$output_dir" && APPIMAGE_EXTRACT_AND_RUN=1 "$linuxdeploy" --appdir "$appdir" --desktop-file "$appdir/dlna-server.desktop" --icon-file "$appdir/dlna-server.svg" --output appimage)
appimage=$(find "$output_dir" -maxdepth 1 -type f -name '*.AppImage' | head -n 1)
if [ -z "$appimage" ]; then
    echo "AppImage build did not produce an AppImage in $output_dir" >&2
    exit 1
fi
mv "$appimage" "$output_dir/DLNA_Server-${version}-x86_64.AppImage"

flatpak_builder=${FLATPAK_BUILDER:-$(command -v flatpak-builder || true)}
flatpak=${FLATPAK:-$(command -v flatpak || true)}
if [ -z "$flatpak_builder" ] || [ -z "$flatpak" ]; then
    echo "flatpak-builder and flatpak are required for default release assets" >&2
    exit 1
fi

flatpak_repo="$release_stage_dir/flatpak-repo"
flatpak_build="$release_stage_dir/flatpak-build"
flatpak_bundle="$output_dir/dlna-server-${version}-linux-x86_64.flatpak"
rm -rf "$flatpak_build" "$flatpak_repo" "$flatpak_bundle"
"$flatpak_builder" --force-clean --repo="$flatpak_repo" "$flatpak_build" "$repo_root/packaging/flatpak/com.github.14ag.dlna_server.yml"
"$flatpak" build-bundle "$flatpak_repo" "$flatpak_bundle" com.github.14ag.dlna_server stable

for required in \
    "$output_dir"/dlna-server_"$version"_*.deb \
    "$output_dir/DLNA_Server-${version}-x86_64.AppImage" \
    "$flatpak_bundle"; do
    if ! compgen -G "$required" >/dev/null; then
        echo "Required Linux release artifact missing: $required" >&2
        exit 1
    fi
done
