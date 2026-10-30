# hdmiosd

Linux 下用于 HDMI/DisplayPort 显示器的 DDC/CI 屏上控制器（C++ / wxWidgets）。

`hdmiosd` 通过 `/dev/i2c-*` 直接收发 DDC/CI（不调用 `ddcutil`），并以画布绘制
扁平 OSD；Tabler 图标经 bas-cpp assets 嵌入。

## Device OSD 与 App OSD

| 菜单 | 作用 |
|------|------|
| **OSD**（以及亮度/图像/色彩等） | 控制**显示器**（VCP） |
| **App OSD** | 控制 **hdmiosd 窗口**：不透明度、尺寸、边距定位 |

App OSD 与上次显示器、快捷键保存在：

```text
~/.config/hdmiosd/settings.ini
```

## 用法

```bash
hdmiosd [选项]
hdmiosd --list
hdmiosd -d /dev/i2c-15 -o 90 -r 24 -b 48
```

访问 I2C 通常需要加入 `i2c` 组（或以 root 运行）。

## 构建

```bash
sudo apt install meson ninja-build g++ pkg-config libbas-cpp-dev libbas-ui-dev \
  libwxgtk3.2-dev asciidoctor zip binutils
meson setup /build
ninja -C /build
meson test -C /build
```

## 许可证

Copyright (C) 2026 Lenik <hdmiosd@bodz.net>

采用 **AGPL-3.0-or-later** 许可。完整文本见 `LICENSE`。
