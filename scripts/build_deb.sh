#!/usr/bin/env bash
# build_deb.sh — Build Debian package (.deb) into output/linux
# Delegates to build_linux.sh --deb so the package is built in the same
# cmake run as the binaries, avoiding a redundant second compile.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
bash "$script_dir/build_linux.sh" --deb
