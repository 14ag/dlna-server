#!/usr/bin/env bash
set -euo pipefail

repo_root=${DLNA_REPO_ROOT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)}
version=${DLNA_SERVER_VERSION:-$(grep -E '^project\(dlna-server VERSION ' "$repo_root/CMakeLists.txt" | sed -E 's/.*VERSION ([0-9.]+).*/\1/')}
build_dir=${DLNA_LINUX_BUILD_DIR:-"$repo_root/build-release-linux"}
install_dir=${DLNA_LINUX_INSTALL_DIR:-"$repo_root/output/linux"}
output_dir=${DLNA_OUTPUT_DIR:-"$repo_root/output"}
appdir="$output_dir/dlna-server.AppDir"
tools_dir="$output_dir/tools"

mkdir -p "$output_dir" "$tools_dir"
find "$output_dir" -maxdepth 1 -type f \( \
    -name "dlna-server_${version}_*.deb" -o \
    -name "dlna-server-${version}-linux-*" -o \
    -name "DLNA_Server-${version}-x86_64.AppImage" \
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
    if [ ! -x "$linuxdeploy" ]; then
        if command -v curl >/dev/null 2>&1; then
            curl -L -o "$linuxdeploy" "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" || true
        elif command -v wget >/dev/null 2>&1; then
            wget -O "$linuxdeploy" "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" || true
        fi
        chmod +x "$linuxdeploy" 2>/dev/null || true
    fi
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
        if [ ! -s "$runtime_file" ] && command -v curl >/dev/null 2>&1; then
            curl -L -o "$runtime_file" "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64" || true
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
    rm -rf "$flatpak_build" "$flatpak_repo"
    "$flatpak_builder" --force-clean --repo="$flatpak_repo" "$flatpak_build" "$repo_root/packaging/flatpak/com.github.14ag.dlna_server.yml"
    "$flatpak" build-bundle "$flatpak_repo" "$output_dir/dlna-server-${version}-linux-x86_64.flatpak" com.github.14ag.dlna_server stable
else
    echo "WARN: flatpak-builder unavailable; Flatpak bundle skipped" >&2
fi
