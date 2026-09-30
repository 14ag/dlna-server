#!/usr/bin/env bash
# build_flatpak.sh — Build Flatpak bundle in output/linux
set -euo pipefail

export PATH="/usr/bin:/bin:/usr/sbin:/sbin"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version=$(grep -E '^project\(dlna-server VERSION ' "$repo_root/CMakeLists.txt" | sed -E 's/.*VERSION ([0-9.]+).*/\1/')
output_dir="$repo_root/output/linux"

build_root=$(mktemp -d "${TMPDIR:-/tmp}/dlna-server-flatpak-build.XXXXXX")
trap 'rm -rf "$build_root"' EXIT

flatpak_repo="$build_root/flatpak-repo"
flatpak_build="$build_root/flatpak-build"
flatpak_bundle="$output_dir/dlna-server-${version}-linux-x86_64.flatpak"

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

mkdir -p "$output_dir"

_pkgs=(flatpak flatpak-builder)
_need=false
for _p in "${_pkgs[@]}"; do
    if ! dpkg -s "$_p" &>/dev/null; then _need=true; break; fi
done
if $_need; then
    echo "[INFO] Installing flatpak prerequisites..."
    sudo_run env DEBIAN_FRONTEND=noninteractive apt-get update -qq
    sudo_run env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${_pkgs[@]}"
fi

sudo_run flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
if ! flatpak info org.freedesktop.Platform//24.08 >/dev/null 2>&1; then
    sudo_run flatpak install -y flathub org.freedesktop.Platform//24.08 org.freedesktop.Sdk//24.08
fi
if ! flatpak info org.gnome.Platform//47 >/dev/null 2>&1; then
    sudo_run flatpak install -y flathub org.gnome.Platform//47 org.gnome.Sdk//47
fi

rm -rf "$flatpak_build" "$flatpak_repo" "$flatpak_bundle"

tmp_quarantine=""
if [ -d "$repo_root/tmp" ]; then
    tmp_quarantine=$(mktemp -d /tmp/dlna-server-pytest.XXXXXX)
    mv "$repo_root/tmp" "$tmp_quarantine/pytest-tmp"
    mkdir -m 700 "$repo_root/tmp"
fi

flatpak-builder --force-clean --disable-rofiles-fuse --state-dir="$build_root/flatpak-state" "$flatpak_build" "$repo_root/packaging/flatpak/com.github.dlna-server-14ag.yml" || true
mkdir -p "$flatpak_build/app/share/appdata" "$flatpak_build/app/share/metainfo"
cp -f "$repo_root/packaging/flatpak/com.github.dlna-server-14ag.metainfo.xml" "$flatpak_build/app/share/metainfo/com.github.dlna-server-14ag.metainfo.xml"
cp -f "$repo_root/packaging/flatpak/com.github.dlna-server-14ag.metainfo.xml" "$flatpak_build/app/share/appdata/com.github.dlna-server-14ag.appdata.xml"
flatpak build-export "$flatpak_repo" "$flatpak_build" stable

rm -rf "$repo_root/tmp"
install -Dm644 "$repo_root/packaging/flatpak/com.github.dlna-server-14ag.metainfo.xml" \
    "$flatpak_build/app/share/metainfo/com.github.dlna-server-14ag.metainfo.xml"
flatpak build-export "$flatpak_repo" "$flatpak_build" stable
flatpak build-bundle "$flatpak_repo" "$flatpak_bundle" com.github.dlna-server-14ag stable

echo "Flatpak bundle created: $flatpak_bundle"

