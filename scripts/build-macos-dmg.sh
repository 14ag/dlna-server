#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version=${DLNA_SERVER_VERSION:-$(grep -E '^project\(dlna-server VERSION ' "$repo_root/CMakeLists.txt" | sed -E 's/.*VERSION ([0-9.]+).*/\1/')}
arch=${DLNA_MACOS_ARCH:-$(uname -m)}
platform_name=${DLNA_MACOS_PLATFORM_NAME:-}
if [ -z "$platform_name" ]; then
    if [ "$arch" = "x86_64" ]; then
        platform_name=macos-x64
    else
        platform_name=macos-$arch
    fi
fi
artifact_arch=$arch
if [ "$arch" = "x86_64" ]; then
    artifact_arch=x64
fi
platform_dir=${DLNA_MACOS_PLATFORM_DIR:-"$repo_root/output/$platform_name"}
build_dir=${DLNA_MACOS_BUILD_DIR:-"$repo_root/build-release-macos-$arch"}
install_dir=${DLNA_MACOS_INSTALL_DIR:-"$platform_dir/install"}

case "$platform_dir" in /*) ;; *) platform_dir="$repo_root/$platform_dir" ;; esac
case "$build_dir" in /*) ;; *) build_dir="$repo_root/$build_dir" ;; esac
case "$install_dir" in /*) ;; *) install_dir="$repo_root/$install_dir" ;; esac

if [ "${DLNA_NO_CLEAN:-0}" != "1" ]; then
    case "$platform_dir" in
        "$repo_root"/output/*) rm -rf "$platform_dir" ;;
        *) echo "Refusing to clean non-output platform dir: $platform_dir" >&2; exit 1 ;;
    esac
fi
mkdir -p "$platform_dir"

cmake -S "$repo_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$install_dir" \
    -DDLNA_ENABLE_FLTK_GUI=ON
cmake --build "$build_dir" --parallel "${DLNA_BUILD_JOBS:-2}"
cmake --install "$build_dir"

app_path="$install_dir/DLNA Server.app"
if [ ! -d "$app_path" ]; then
    echo "DLNA Server.app missing at $app_path" >&2
    exit 1
fi

if [ -n "${APPLE_DEVELOPER_ID:-}" ]; then
    codesign --force --deep --options runtime --timestamp --sign "$APPLE_DEVELOPER_ID" "$app_path"
fi

dmg_path="$platform_dir/DLNA_Server-${version}-macos-${artifact_arch}.dmg"
rm -f "$dmg_path"
hdiutil create -volname "DLNA Server" -srcfolder "$app_path" -ov -format UDZO "$dmg_path"

if [ -n "${APPLE_DEVELOPER_ID:-}" ]; then
    codesign --force --timestamp --sign "$APPLE_DEVELOPER_ID" "$dmg_path"
fi

if [ -n "${APPLE_NOTARY_APPLE_ID:-}" ] && [ -n "${APPLE_NOTARY_PASSWORD:-}" ] && [ -n "${APPLE_TEAM_ID:-}" ]; then
    xcrun notarytool submit "$dmg_path" \
        --apple-id "$APPLE_NOTARY_APPLE_ID" \
        --password "$APPLE_NOTARY_PASSWORD" \
        --team-id "$APPLE_TEAM_ID" \
        --wait
    xcrun stapler staple "$dmg_path"
fi
