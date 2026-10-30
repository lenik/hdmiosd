/*
 * Copyright (C) 2026 Lenik <hdmiosd@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vcp.hpp"

#include <cstdio>
#include <cstring>

static int failures;

static void expect_eq_int(const char* name, int got, int want) {
    if (got != want) {
        fprintf(stderr, "FAIL %s: got %d want %d\n", name, got, want);
        failures++;
    }
}

static void expect_true(const char* name, bool cond) {
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", name);
        failures++;
    }
}

int main() {
    VcpCaps caps;
    const char* sample =
        "(prot(monitor)type(LCD)model(QHD)cmds(01 02 03 07 0C E3 F3)"
        "vcp(02 04 10 12 14(01 02 04 05) 60(0F 11) 87)mswhql(1))";
    vcp_caps_parse(caps, sample);
    expect_true("caps brightness", vcp_caps_supports(caps, 0x10) > 0);
    expect_true("caps contrast", vcp_caps_supports(caps, 0x12) > 0);
    expect_true("caps no audio mute", vcp_caps_supports(caps, 0x8d) == 0);
    expect_true("cat brightness", vcp_caps_category_supported(caps, VcpCategory::Brightness));
    expect_true("no audio", !vcp_caps_category_supported(caps, VcpCategory::Audio));
    expect_true("app osd always", vcp_caps_category_supported(caps, VcpCategory::AppOsd));
    expect_eq_int("color temp 6500K", (int)vcp_color_temp_kelvin(35, 100), 6500);
    expect_true("feature brightness", vcp_feature_by_code(0x10) != nullptr);
    expect_eq_int("0x0B RO", (int)vcp_feature_by_code(0x0b)->kind, (int)VcpKind::Ro);
    expect_eq_int("0x0C discrete mode", (int)vcp_feature_by_code(0x0c)->kind, (int)VcpKind::Discrete);
    expect_true("0x0C has modes", vcp_feature_by_code(0x0c)->nchoices >= 4);
    char lab[32];
    expect_true("0x0C 35 is 6500 K",
                strcmp(vcp_color_temp_mode_label(35, 100, lab, sizeof(lab)), "6500 K") == 0);
    expect_true("device osd name", strcmp(vcp_category_name(VcpCategory::DeviceOsd), "OSD") == 0);
    expect_true("app osd name", strcmp(vcp_category_name(VcpCategory::AppOsd), "App OSD") == 0);
    return failures == 0 ? 0 : 1;
}
