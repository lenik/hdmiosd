#ifndef HDMIOOSD_VCP_HPP
#define HDMIOOSD_VCP_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/* Sidebar order matches this enum order. */
enum class VcpCategory {
    Display = 0, /* luminance / contrast / backlight */
    Color,
    Image,
    Presets,
    Input,     /* input source + timing */
    Audio,
    DeviceOsd, /* monitor OSD language / button control */
    SoftOsd,   /* hdmiosd window — not a VCP category */
    System,    /* power, VCP version, … */
    Count
};

enum class VcpKind { Continuous, Discrete, Button, Ro };

struct VcpChoice {
    uint16_t value;
    const char* label;
};

struct VcpFeature {
    uint8_t code;
    VcpCategory cat;
    VcpKind kind;
    const char* name;
    const char* shortcut;
    const char* icon; /* tabler stem under icons/24/ */
    const VcpChoice* choices;
    size_t nchoices;
    const uint8_t* refresh_codes;
    bool refresh_all;
    const char* description; /* N_() msgid; shown in Soft/Device info pane */
};

struct VcpCaps {
    int present[256]{};
    uint16_t values[256][32]{};
    size_t nvalues[256]{};
    std::string raw;
};

const char* vcp_category_name(VcpCategory cat);
const char* vcp_category_icon(VcpCategory cat);

const std::vector<VcpFeature>& vcp_features();
const VcpFeature* vcp_feature_by_code(uint8_t code);

void vcp_caps_parse(VcpCaps& caps, const std::string& raw);
int vcp_caps_supports(const VcpCaps& caps, uint8_t code);
bool vcp_caps_category_supported(const VcpCaps& caps, VcpCategory cat);

unsigned vcp_color_temp_kelvin(uint16_t request, uint16_t increment);
/* Label for a discrete 0x0C mode; returns catalog label or fills buf with "NNNN K". */
const char* vcp_color_temp_mode_label(uint16_t request, uint16_t increment, char* buf,
                                      size_t buflen);

#endif
