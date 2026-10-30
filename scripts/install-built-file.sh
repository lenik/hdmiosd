#!/usr/bin/env bash
# Install a Meson-built file to $DESTDIR$2 (absolute path under prefix).
set -euo pipefail
src="${1:?built file}"
dest="${2:?absolute install path}"
install -D -m644 "$src" "${DESTDIR:-}${dest}"
