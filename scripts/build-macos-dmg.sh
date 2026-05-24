#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version=${DLNA_SERVER_VERSION:-$(grep -E '^project\(dlna-server VERSION ' "$repo_root/CMakeLists.txt" | sed -E 's/.*VERSION ([0-9.]+).*/\1/')}
arch=${DLNA_MACOS_ARCH:-$(uname -m)}
build_dir=${DLNA_MACOS_BUILD_DIR:-"$repo_root/build-release-macos-$arch"}
install_dir=${DLNA_MACOS_INSTALL_DIR:-"$repo_root/output/macos-$arch"}
output_dir=${DLNA_OUTPUT_DIR:-"$repo_root/output"}

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

dmg_path="$output_dir/DLNA_Server-${version}-macos-${arch}.dmg"
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
