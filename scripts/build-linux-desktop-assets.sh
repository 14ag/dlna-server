#!/usr/bin/env bash
set -euo pipefail

repo_root=${DLNA_REPO_ROOT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)}
version=${DLNA_SERVER_VERSION:-$(grep -E '^project\(dlna-server VERSION ' "$repo_root/CMakeLists.txt" | sed -E 's/.*VERSION ([0-9.]+).*/\1/')}
build_dir=${DLNA_LINUX_BUILD_DIR:-"$repo_root/build-release-linux"}
install_dir=${DLNA_LINUX_INSTALL_DIR:-"$repo_root/output/linux"}
output_dir=${DLNA_OUTPUT_DIR:-"$repo_root/output"}
appdir="$output_dir/dlna-server.AppDir"
tools_dir="$output_dir/tools"
linuxdeploy_version="1-alpha-20251107-1"
linuxdeploy_sha256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
runtime_sha256="a2419dce47568395ae79c01ffa9a5a341dd339581352ff104d073527543177e5"

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
        curl -L -o "$path" "$url" || return 1
    elif command -v wget >/dev/null 2>&1; then
        wget -O "$path" "$url" || return 1
    else
        return 1
    fi

    if [ "$(sha256_file "$path")" != "$expected" ]; then
        rm -f "$path"
        echo "Checksum mismatch for $path" >&2
        return 1
    fi
}

mkdir -p "$output_dir" "$tools_dir"
find "$output_dir" -maxdepth 1 -type f \( \
    -name "dlna-server_${version}_*.deb" -o \
    -name "dlna-server-${version}-linux-*" -o \
    -name "DLNA_Server-${version}-x86_64.AppImage" -o \
    -name "*.AppImage" \
    \) -delete

cmake_args=(
    -S "$repo_root"
    -B "$build_dir"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX="$install_dir"
    -DDLNA_ENABLE_FLTK_GUI=ON
)

if [ -n "${DLNA_FLTK_SOURCE_DIR:-}" ] && [ -d "$DLNA_FLTK_SOURCE_DIR" ]; then
    cmake_args+=("-DFETCHCONTENT_SOURCE_DIR_FLTK=$DLNA_FLTK_SOURCE_DIR")
elif [ -d "$repo_root/build-wsl-native/_deps/fltk-src" ]; then
    cmake_args+=("-DFETCHCONTENT_SOURCE_DIR_FLTK=$repo_root/build-wsl-native/_deps/fltk-src")
fi

cmake "${cmake_args[@]}"
cmake --build "$build_dir" --parallel "${DLNA_BUILD_JOBS:-2}"
cmake --install "$build_dir"

cpack --config "$build_dir/CPackConfig.cmake" -B "$output_dir"

rm -rf "$appdir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share"
cp -a "$install_dir/bin/." "$appdir/usr/bin/"
cp -a "$install_dir/share/." "$appdir/usr/share/"
if command -v curl >/dev/null 2>&1; then
    cp "$(command -v curl)" "$appdir/usr/bin/curl"
fi
cp "$repo_root/packaging/linux/AppRun" "$appdir/AppRun"
tr -d '\r' < "$repo_root/packaging/linux/dlna-server.appimage.desktop" > "$appdir/dlna-server.desktop"
tr -d '\r' < "$repo_root/packaging/linux/dlna-server.appimage.desktop" > "$appdir/usr/share/applications/dlna-server.desktop"
cp "$repo_root/resources/dlna-server.svg" "$appdir/dlna-server.svg"
chmod +x "$appdir/AppRun" "$appdir/usr/bin/dlna-server" "$appdir/usr/bin/dlna-server-gui" "$appdir/usr/bin/dlna-server-gui-bin"

linuxdeploy=${LINUXDEPLOY:-}
if [ -z "$linuxdeploy" ]; then
    linuxdeploy="$tools_dir/linuxdeploy-x86_64.AppImage"
    download_verified "https://github.com/linuxdeploy/linuxdeploy/releases/download/$linuxdeploy_version/linuxdeploy-x86_64.AppImage" "$linuxdeploy" "$linuxdeploy_sha256" || true
    chmod +x "$linuxdeploy" 2>/dev/null || true
fi
chmod +x "$linuxdeploy" 2>/dev/null || true

if [ -x "$linuxdeploy" ]; then
    if ! (cd "$output_dir" && APPIMAGE_EXTRACT_AND_RUN=1 "$linuxdeploy" --appdir "$appdir" --desktop-file "$appdir/dlna-server.desktop" --icon-file "$appdir/dlna-server.svg" --output appimage); then
        echo "WARN: linuxdeploy AppImage plugin failed; trying local appimagetool fallback" >&2
    fi
    appimage=$(find "$output_dir" -maxdepth 1 -type f -name '*.AppImage' | head -n 1 || true)
    if [ -n "$appimage" ]; then
        mv "$appimage" "$output_dir/DLNA_Server-${version}-x86_64.AppImage"
    fi

    if [ ! -f "$output_dir/DLNA_Server-${version}-x86_64.AppImage" ]; then
        runtime_file=${APPIMAGE_RUNTIME:-"$tools_dir/runtime-x86_64"}
        if [ -z "${APPIMAGE_RUNTIME:-}" ] && { [ ! -s "$runtime_file" ] || [ "$(sha256_file "$runtime_file")" != "$runtime_sha256" ]; }; then
            download_verified "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64" "$runtime_file" "$runtime_sha256" || true
        fi
        if [ -s "$runtime_file" ]; then
            extract_dir="$tools_dir/linuxdeploy-extract"
            rm -rf "$extract_dir"
            mkdir -p "$extract_dir"
            (cd "$extract_dir" && "$linuxdeploy" --appimage-extract >/dev/null)
            appimagetool=$(find "$extract_dir" -path '*/linuxdeploy-plugin-appimage/usr/bin/appimagetool' -type f | head -n 1 || true)
            if [ -n "$appimagetool" ]; then
                chmod +x "$appimagetool" "$runtime_file"
                ARCH=x86_64 "$appimagetool" --runtime-file "$runtime_file" "$appdir" "$output_dir/DLNA_Server-${version}-x86_64.AppImage"
            fi
            rm -rf "$extract_dir"
        fi
    fi
else
    echo "WARN: linuxdeploy unavailable; AppImage skipped" >&2
fi

flatpak_builder=${FLATPAK_BUILDER:-$(command -v flatpak-builder || true)}
flatpak=${FLATPAK:-$(command -v flatpak || true)}
if [ -n "$flatpak_builder" ] && [ -n "$flatpak" ]; then
    flatpak_repo="$output_dir/flatpak-repo"
    flatpak_build="$output_dir/flatpak-build"
    flatpak_bundle="$output_dir/dlna-server-${version}-linux-x86_64.flatpak"
    rm -rf "$flatpak_build" "$flatpak_repo"
    if "$flatpak_builder" --force-clean --repo="$flatpak_repo" "$flatpak_build" "$repo_root/packaging/flatpak/com.github.14ag.dlna_server.yml" &&
       "$flatpak" build-bundle "$flatpak_repo" "$flatpak_bundle" com.github.14ag.dlna_server stable; then
        :
    else
        rm -f "$flatpak_bundle"
        echo "WARN: Flatpak build failed; Flatpak bundle skipped" >&2
    fi
else
    echo "WARN: flatpak-builder unavailable; Flatpak bundle skipped" >&2
fi
