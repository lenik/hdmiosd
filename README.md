# hdmiosd

On-screen DDC/CI controller for HDMI/DisplayPort monitors on Linux
(C++ / wxWidgets).

`hdmiosd` talks DDC/CI directly over `/dev/i2c-*` (no `ddcutil` process) and
draws a flat canvas OSD with Tabler icons embedded via bas-cpp assets.

## Screenshots

Device Color controls with live **Information** for the highlighted row:

![Color tab](screenshots/screenshot1.png)

**Soft OSD** — opacity, size, and monitor-edge margins:

![Soft OSD](screenshots/screenshot2.png)

Localized CLI help (`langtour hdmiosd -h`):

![CLI help](screenshots/cmdline.png)

## Device menus vs Soft OSD

| Menu | Purpose |
|------|---------|
| **Display**, **Color**, **Image**, **Presets**, **Input**, **Audio**, **OSD**, **System** | Control the **monitor** via VCP (empty tabs stay hidden) |
| **Soft OSD** | Control the **hdmiosd window**: opacity, size, placement |

Sidebar order matches the table. Soft OSD values (and last display / shortcuts)
are stored in:

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

| Keys | Action |
|------|--------|
| `1`–`N` | Select sidebar tab |
| `Tab` / `Shift+Tab` | Next / previous control in the tab |
| `←` `→` / `-` `+` | Adjust value |
| `b` `c` … | Feature shortcuts (see row labels) |
| `Ctrl+Z` / `Ctrl+R` | Undo / reset active control |
| `F5` | Refresh displays and controls |
| `F11` / `F12` | Previous / next theme |
| `[` / `]` | Previous / next display |
| Drag blank area | Move Soft OSD (nearest-edge margins update live) |
| `Esc` / `q` | Quit |

Defaults live under `[shortcuts]` in `settings.ini` and can be customized there.

## Build

```bash
sudo apt install meson ninja-build g++ pkg-config libbas-cpp-dev libbas-ui-dev \
  libwxgtk3.2-dev asciidoctor zip binutils
meson setup build
ninja -C build
meson test -C build
```

Icons: copy selected Tabler PNGs into `assets/icons/{16,24}/`; Meson zips them
into an embedded `assets.zip` (`src/assets.s` + `define_zip_assets`).

## License

Copyright (C) 2026 Lenik <hdmiosd@bodz.net>

Licensed under **AGPL-3.0-or-later**. See `LICENSE`.
