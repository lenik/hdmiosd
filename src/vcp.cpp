/*
 * Copyright (C) 2026 Lenik <hdmiosd@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vcp.hpp"

#include <bas/locale/i18n.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <iterator>

static const VcpChoice color_presets[] = {
    {0x01, N_("sRGB")}, {0x02, N_("Native")}, {0x04, N_("5000 K")}, {0x05, N_("6500 K")},
    {0x06, N_("7500 K")}, {0x08, N_("9300 K")}, {0x0a, N_("User")}, {0x0b, N_("User 1")},
};

/*
 * VCP 0x0C (MCCS "Color Temperature Request") is often implemented as a
 * discrete mode index that snaps to standard white points — not a smooth
 * Kelvin slider. Request codes below assume increment 0x0B = 100 K with
 * base 3000 K: K = 3000 + request * 100.
 */
static const VcpChoice color_temp_modes[] = {
    {20, N_("5000 K")}, {30, N_("6000 K")}, {35, N_("6500 K")},
    {45, N_("7500 K")}, {55, N_("8500 K")}, {63, N_("9300 K")},
};
static const VcpChoice input_sources[] = {
    {0x01, N_("VGA-1")}, {0x03, N_("DVI-1")}, {0x04, N_("DVI-2")}, {0x0f, N_("DisplayPort-1")},
    {0x10, N_("DisplayPort-2")}, {0x11, N_("HDMI-1")}, {0x12, N_("HDMI-2")},
};
static const VcpChoice osd_lang[] = {
    {0x01, N_("Chinese (trad.)")}, {0x02, N_("English")}, {0x03, N_("French")}, {0x04, N_("German")},
    {0x06, N_("Japanese")}, {0x0a, N_("Spanish")}, {0x0d, N_("Chinese (simp.)")},
};
static const VcpChoice power_modes[] = {
    {0x01, N_("On")}, {0x04, N_("Off")}, {0x05, N_("Power off")},
};
static const VcpChoice mute_vals[] = {{0x01, N_("Muted")}, {0x02, N_("Unmuted")}};

static const uint8_t refresh_bri[] = {0x10, 0x12, 0x13, 0x6b, 0};
static const uint8_t refresh_geo[] = {0x86, 0x87, 0xda, 0xdc, 0};
static const uint8_t refresh_color[] = {0x14, 0x0c, 0x0b, 0x16, 0x18, 0x1a, 0x6c, 0x6d, 0x6e, 0};

static const VcpFeature catalog[] = {
    {0x10, VcpCategory::Display, VcpKind::Continuous, N_("Brightness"), "b", "brightness", nullptr, 0,
     nullptr, false, N_("Overall image luminance. Higher values look brighter.")},
    {0x12, VcpCategory::Display, VcpKind::Continuous, N_("Contrast"), "c", "contrast", nullptr, 0,
     nullptr, false, N_("Difference between dark and bright areas of the image.")},
    {0x13, VcpCategory::Display, VcpKind::Continuous, N_("Backlight"), nullptr, "sun", nullptr, 0,
     nullptr, false, N_("Panel backlight level when exposed separately from brightness.")},
    {0x6b, VcpCategory::Display, VcpKind::Continuous, N_("Backlight white"), nullptr, "sun", nullptr, 0,
     nullptr, false, N_("White-channel backlight level on some LED panels.")},

    {0x14, VcpCategory::Color, VcpKind::Discrete, N_("Color preset"), "t", "palette", color_presets,
     sizeof(color_presets) / sizeof(color_presets[0]), nullptr, false,
     N_("Named white-point / picture mode from the monitor (sRGB, Native, …).")},
    {0x0c, VcpCategory::Color, VcpKind::Discrete, N_("Color temp mode"), nullptr, "temperature",
     color_temp_modes, sizeof(color_temp_modes) / sizeof(color_temp_modes[0]), nullptr, false,
     N_("Discrete color-temperature request (standard Kelvin white points).")},
    {0x0b, VcpCategory::Color, VcpKind::Ro, N_("Color temp. step"), nullptr, "temperature", nullptr, 0,
     nullptr, false, N_("Reported Kelvin step size used with color-temperature request.")},
    {0x16, VcpCategory::Color, VcpKind::Continuous, N_("Gain red"), nullptr, "palette", nullptr, 0,
     nullptr, false, N_("Red channel gain (drive) for the user color mode.")},
    {0x18, VcpCategory::Color, VcpKind::Continuous, N_("Gain green"), nullptr, "palette", nullptr, 0,
     nullptr, false, N_("Green channel gain (drive) for the user color mode.")},
    {0x1a, VcpCategory::Color, VcpKind::Continuous, N_("Gain blue"), nullptr, "palette", nullptr, 0,
     nullptr, false, N_("Blue channel gain (drive) for the user color mode.")},
    {0x6c, VcpCategory::Color, VcpKind::Continuous, N_("Black level red"), nullptr, "palette", nullptr, 0,
     nullptr, false, N_("Red channel black level (offset / cut-off).")},
    {0x6d, VcpCategory::Color, VcpKind::Continuous, N_("Black level green"), nullptr, "palette", nullptr,
     0, nullptr, false, N_("Green channel black level (offset / cut-off).")},
    {0x6e, VcpCategory::Color, VcpKind::Continuous, N_("Black level blue"), nullptr, "palette", nullptr, 0,
     nullptr, false, N_("Blue channel black level (offset / cut-off).")},

    {0x87, VcpCategory::Image, VcpKind::Continuous, N_("Sharpness"), "s", "adjustments", nullptr, 0,
     nullptr, false, N_("Edge enhancement / sharpness of the image.")},
    {0x86, VcpCategory::Image, VcpKind::Discrete, N_("Display scaling"), nullptr, "photo", nullptr, 0,
     nullptr, false, N_("How the source is scaled to the panel (aspect, full, …).")},
    {0xda, VcpCategory::Image, VcpKind::Discrete, N_("Scan mode"), nullptr, "photo", nullptr, 0, nullptr,
     false, N_("Overscan / underscan presentation of the video signal.")},
    {0xdc, VcpCategory::Image, VcpKind::Discrete, N_("Display application"), nullptr, "apps", nullptr, 0,
     nullptr, false, N_("Monitor picture application mode when exposed via VCP.")},
    {0xb2, VcpCategory::Image, VcpKind::Ro, N_("Sub-pixel layout"), nullptr, "layout-sidebar", nullptr, 0,
     nullptr, false, N_("Reported RGB sub-pixel arrangement of the panel.")},
    {0xb6, VcpCategory::Image, VcpKind::Ro, N_("Technology type"), nullptr, "device-desktop", nullptr, 0,
     nullptr, false, N_("Reported display technology (LCD, OLED, …).")},

    {0x04, VcpCategory::Presets, VcpKind::Button, N_("Factory defaults"), "0", "restore", nullptr, 0,
     nullptr, true, N_("Restore all monitor settings to factory defaults.")},
    {0x05, VcpCategory::Presets, VcpKind::Button, N_("Factory brightness/contrast"), nullptr, "brightness",
     nullptr, 0, refresh_bri, false, N_("Restore factory brightness and contrast values.")},
    {0x06, VcpCategory::Presets, VcpKind::Button, N_("Factory geometry"), nullptr, "photo", nullptr, 0,
     refresh_geo, false, N_("Restore factory geometry / scaling related settings.")},
    {0x08, VcpCategory::Presets, VcpKind::Button, N_("Factory color"), nullptr, "palette", nullptr, 0,
     refresh_color, false, N_("Restore factory color gains and related settings.")},

    {0x60, VcpCategory::Input, VcpKind::Discrete, N_("Input source"), "i", "device-desktop", input_sources,
     sizeof(input_sources) / sizeof(input_sources[0]), nullptr, false,
     N_("Active video input (HDMI, DisplayPort, DVI, VGA, …).")},
    {0xac, VcpCategory::Input, VcpKind::Ro, N_("Horizontal frequency"), nullptr, "gauge", nullptr, 0,
     nullptr, false, N_("Reported horizontal sync frequency of the current input.")},
    {0xae, VcpCategory::Input, VcpKind::Ro, N_("Vertical frequency"), nullptr, "gauge", nullptr, 0, nullptr,
     false, N_("Reported vertical refresh rate of the current input.")},

    {0x62, VcpCategory::Audio, VcpKind::Continuous, N_("Speaker volume"), "v", "volume", nullptr, 0,
     nullptr, false, N_("Built-in speaker volume when the monitor exposes audio VCP.")},
    {0x8d, VcpCategory::Audio, VcpKind::Discrete, N_("Audio mute"), "m", "volume", mute_vals,
     sizeof(mute_vals) / sizeof(mute_vals[0]), nullptr, false, N_("Mute or unmute monitor speakers.")},
    {0x91, VcpCategory::Audio, VcpKind::Continuous, N_("Treble"), nullptr, "volume", nullptr, 0, nullptr,
     false, N_("High-frequency audio tone control.")},
    {0x92, VcpCategory::Audio, VcpKind::Continuous, N_("Bass"), nullptr, "volume", nullptr, 0, nullptr,
     false, N_("Low-frequency audio tone control.")},
    {0x93, VcpCategory::Audio, VcpKind::Continuous, N_("Balance"), "a", "adjustments", nullptr, 0, nullptr,
     false, N_("Left/right speaker balance.")},

    {0xca, VcpCategory::DeviceOsd, VcpKind::Discrete, N_("OSD control"), nullptr, "device-desktop", nullptr,
     0, nullptr, false, N_("Enable or lock the monitor's own on-screen menu.")},
    {0xcc, VcpCategory::DeviceOsd, VcpKind::Discrete, N_("OSD language"), "l", "language", osd_lang,
     sizeof(osd_lang) / sizeof(osd_lang[0]), nullptr, false,
     N_("Language of the monitor's built-in OSD.")},

    {0xd6, VcpCategory::System, VcpKind::Discrete, N_("Power mode"), "p", "gauge", power_modes,
     sizeof(power_modes) / sizeof(power_modes[0]), nullptr, false,
     N_("Monitor power state (on / off / power-off).")},
    {0xdf, VcpCategory::System, VcpKind::Ro, N_("VCP version"), nullptr, "settings", nullptr, 0, nullptr,
     false, N_("MCCS / VCP version reported by the display.")},
};

const char* vcp_category_name(VcpCategory cat) {
    switch (cat) {
    case VcpCategory::Display:
        return N_("Display");
    case VcpCategory::Color:
        return N_("Color");
    case VcpCategory::Image:
        return N_("Image");
    case VcpCategory::Presets:
        return N_("Presets");
    case VcpCategory::Input:
        return N_("Input");
    case VcpCategory::Audio:
        return N_("Audio");
    case VcpCategory::DeviceOsd:
        return N_("OSD");
    case VcpCategory::SoftOsd:
        return N_("Soft OSD");
    case VcpCategory::System:
        return N_("System");
    default:
        return N_("Unknown");
    }
}

const char* vcp_category_icon(VcpCategory cat) {
    switch (cat) {
    case VcpCategory::Display:
        return "brightness";
    case VcpCategory::Color:
        return "palette";
    case VcpCategory::Image:
        return "photo";
    case VcpCategory::Presets:
        return "restore";
    case VcpCategory::Input:
        return "device-desktop";
    case VcpCategory::Audio:
        return "volume";
    case VcpCategory::DeviceOsd:
        return "language";
    case VcpCategory::SoftOsd:
        return "apps";
    case VcpCategory::System:
        return "settings";
    default:
        return "settings";
    }
}

const std::vector<VcpFeature>& vcp_features() {
    static std::vector<VcpFeature> feats(std::begin(catalog), std::end(catalog));
    return feats;
}

const VcpFeature* vcp_feature_by_code(uint8_t code) {
    for (const auto& f : catalog) {
        if (f.code == code) {
            return &f;
        }
    }
    return nullptr;
}

static int parse_hex_byte(const char** pp, unsigned* out) {
    const char* p = *pp;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (!isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1])) {
        return -1;
    }
    unsigned v = 0;
    sscanf(p, "%2x", &v);
    *out = v;
    *pp = p + 2;
    return 0;
}

void vcp_caps_parse(VcpCaps& caps, const std::string& raw) {
    caps = {};
    caps.raw = raw;
    const char* vcp = strstr(raw.c_str(), "vcp(");
    if (!vcp) {
        vcp = strstr(raw.c_str(), "VCP(");
    }
    if (!vcp) {
        return;
    }
    vcp += 4;
    int depth = 1;
    while (*vcp && depth > 0) {
        while (*vcp && isspace((unsigned char)*vcp)) {
            vcp++;
        }
        if (*vcp == '(') {
            depth++;
            vcp++;
            continue;
        }
        if (*vcp == ')') {
            depth--;
            vcp++;
            continue;
        }
        unsigned code = 0;
        if (parse_hex_byte(&vcp, &code) != 0) {
            vcp++;
            continue;
        }
        if (code > 255) {
            continue;
        }
        caps.present[code] = 1;
        while (*vcp && isspace((unsigned char)*vcp)) {
            vcp++;
        }
        if (*vcp == '(') {
            vcp++;
            while (*vcp && *vcp != ')') {
                while (*vcp && isspace((unsigned char)*vcp)) {
                    vcp++;
                }
                if (*vcp == ')') {
                    break;
                }
                unsigned val = 0;
                if (parse_hex_byte(&vcp, &val) != 0) {
                    vcp++;
                    continue;
                }
                size_t n = caps.nvalues[code];
                if (n < 32) {
                    caps.values[code][n] = (uint16_t)val;
                    caps.nvalues[code] = n + 1;
                }
            }
            if (*vcp == ')') {
                vcp++;
            }
        }
    }
}

int vcp_caps_supports(const VcpCaps& caps, uint8_t code) {
    if (caps.raw.empty()) {
        return -1;
    }
    return caps.present[code];
}

bool vcp_caps_category_supported(const VcpCaps& caps, VcpCategory cat) {
    if (cat == VcpCategory::SoftOsd) {
        return true;
    }
    for (const auto& f : vcp_features()) {
        if (f.cat != cat) {
            continue;
        }
        int s = vcp_caps_supports(caps, f.code);
        if (s > 0 || s < 0) {
            return true;
        }
    }
    return false;
}

unsigned vcp_color_temp_kelvin(uint16_t request, uint16_t increment) {
    unsigned step = increment ? increment : 100;
    return 3000u + (unsigned)request * step;
}

const char* vcp_color_temp_mode_label(uint16_t request, uint16_t increment, char* buf,
                                      size_t buflen) {
    for (size_t i = 0; i < sizeof(color_temp_modes) / sizeof(color_temp_modes[0]); i++) {
        if (color_temp_modes[i].value == request) {
            return color_temp_modes[i].label;
        }
    }
    if (!buf || buflen < 8) {
        return "custom";
    }
    snprintf(buf, buflen, "%u K", vcp_color_temp_kelvin(request, increment));
    return buf;
}
