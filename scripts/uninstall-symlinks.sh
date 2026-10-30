#!/usr/bin/env bash
# Remove live-build symlinks installed by scripts/install-symlinks.sh.
set -euo pipefail

bindir="${1:?bindir}"
datadir="${2:?datadir}"
mandir="${3:?mandir}"

for p in \
    "$bindir/hdmiosd" \
    "$mandir/man1/hdmiosd.1" \
    "$datadir/bash-completion/completions/hdmiosd"
do
    if [ -L "$p" ]; then
        sudo rm -f "$p"
        printf 'Removed %s\n' "$p"
    else
        printf 'Skipped (not a symlink): %s\n' "$p"
    fi
done
