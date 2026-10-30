# hdmiosd

On-screen DDC/CI controller for HDMI/DisplayPort monitors on Linux
(C++ / wxWidgets).

`hdmiosd` talks DDC/CI directly over `/dev/i2c-*` (no `ddcutil` process) and
draws a flat canvas OSD with Tabler icons embedded via bas-cpp assets.

## Device OSD vs App OSD

| Menu | Purpose |
|------|---------|
| **OSD** (and Brightness / Image / Color / …) | Control the **monitor** via VCP |
| **App OSD** | Control the **hdmiosd window**: opacity, size, placement |

App OSD values (and last display / shortcuts) are stored in:

```text
~/.config/hdmiosd/settings.ini
```

## Usage

```bash
hdmiosd [OPTIONS]
hdmiosd --list
hdmiosd -d /dev/i2c-15 -o 90 -r 24 -b 48
```

| Option | Meaning |
|--------|---------|
| `-d`/`--device` | `/dev/i2c-*`, DRM name, PCI/USB id, or `/sys/...` |
| `-o`/`--opacity` | 0–100 (overrides settings for this run) |
| `-g`/`--geom` | Window size |
| `-l`/`-r`/`-t`/`-b` | Placement margins |
| `-L`/`--list` | List DDC displays |

I2C access typically requires the `i2c` group (or root).

## Shortcuts

`1`–`N` tabs, `Tab` cycle, `←`/`→` adjust, `b`/`c`/… features, `Ctrl+Z` undo,
`F5`/`F6` refresh, `[`/`]` displays, `Esc` quit.  
Defaults live under `[shortcuts]` in `settings.ini` and can be customized there.

## Build

```bash
sudo apt install meson ninja-build g++ pkg-config libbas-cpp-dev libbas-ui-dev \
  libwxgtk3.2-dev asciidoctor zip binutils
meson setup /build
ninja -C /build
meson test -C /build
```

Icons: copy selected Tabler PNGs into `assets/icons/{16,24}/`; Meson zips them
into an embedded `assets.zip` (`src/assets.s` + `define_zip_assets`).

## License

Copyright (C) 2026 Lenik <hdmiosd@bodz.net>

Licensed under **AGPL-3.0-or-later**. See `LICENSE`.
