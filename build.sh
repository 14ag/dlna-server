#!/usr/bin/env bash
# build.sh - Linux orchestrator entry point
#
# Usage:
#   ./build.sh                  # Builds raw binaries into output/linux/ (dlna-server, dlna-server-gui, dlna-server-gui-bin)
#   ./build.sh --deb            # Builds the Debian package (.deb) into output/linux/
#   ./build.sh --appimage       # Builds the .AppImage bundle into output/linux/
#   ./build.sh --flatpak        # Builds the Flatpak bundle into output/linux/
#   ./build.sh --install        # Removes existing dlna-server and installs the newly built package
#   ./build.sh --version=<tag>  # Uses this version tag instead of generating one
#   ./build.sh --build <N>      # Appends -build<N> to the generated version tag
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

INSTALL=0
BUILD_DEB=0
BUILD_APPIMAGE=0
BUILD_FLATPAK=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --install)
            INSTALL=1
            shift
            ;;
        --deb)
            BUILD_DEB=1
            shift
            ;;
        --appimage)
            BUILD_APPIMAGE=1
            shift
            ;;
        --flatpak)
            BUILD_FLATPAK=1
            shift
            ;;
        --build)
            export DLNA_BUILD="${2:?--build needs a number}"
            shift 2
            ;;
        --build=*)
            export DLNA_BUILD="${1#*=}"
            shift
            ;;
        --version=*)
            DLNA_VERSION_TAG="${1#*=}"
            shift
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

# Step 1: Build core Linux binaries (and .deb in the same cmake run when needed)
# Passing --deb here avoids a second full cmake configure+build that would occur
# if build_deb.sh were called separately after build_linux.sh.
source "$script_dir/scripts/lib_identity.sh"
resolve_version
echo "Version: $DLNA_VERSION_TAG"

_linux_args=()
if [ "$BUILD_DEB" = "1" ] || [ "$INSTALL" = "1" ]; then
    _linux_args+=(--deb)
fi
echo "Building Linux binary assets..."
bash "$script_dir/scripts/build_linux.sh" "${_linux_args[@]}"

# Step 3: Build AppImage if requested
if [ "$BUILD_APPIMAGE" = "1" ]; then
    echo "Building AppImage package..."
    bash "$script_dir/scripts/build_appimage.sh"
fi

# Step 4: Build Flatpak bundle if requested
if [ "$BUILD_FLATPAK" = "1" ]; then
    echo "Building Flatpak bundle..."
    bash "$script_dir/scripts/build_flatpak.sh"
fi

# Step 5: Install if requested (removes old installation first, then installs)
if [ "$INSTALL" = "1" ]; then
    echo "Installing Linux application..."
    bash "$script_dir/scripts/install_linux.sh"
fi
