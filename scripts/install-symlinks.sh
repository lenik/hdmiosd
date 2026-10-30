#!/usr/bin/env bash
# Install live-build symlinks for local development (meson run_target).
set -euo pipefail

prefix="${1:?prefix}"
bindir="${2:?bindir}"
datadir="${3:?datadir}"
mandir="${4:?mandir}"
build_root="${5:?build_root}"
source_root="${6:?source_root}"

mkdir -p "$bindir" "$datadir/bash-completion/completions" "$mandir/man1"
sudo ln -sfn "$build_root/hdmiosd" "$bindir/hdmiosd"
sudo ln -sfn "$build_root/hdmiosd.1" "$mandir/man1/hdmiosd.1"
sudo ln -sfn "$source_root/hdmiosd.bash" "$datadir/bash-completion/completions/hdmiosd"
printf 'Symlinks installed under %s\n' "$prefix"
