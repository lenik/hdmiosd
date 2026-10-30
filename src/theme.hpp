#ifndef HDMIOOSD_THEME_HPP
#define HDMIOOSD_THEME_HPP

#include <wx/colour.h>
#include <wx/font.h>
#include <wx/string.h>

#include <string>
#include <vector>

/* Semantic OSD style classes — mapped from VS Code / minor-themes keys. */
struct OsdPalette {
    /* Surfaces */
    wxColour editor_bg{14, 22, 36};     /* editor.background */
    wxColour panel_bg{18, 30, 48};      /* titleBar / section */
    wxColour sidebar_bg{20, 34, 52};    /* sideBar.background */
    wxColour status_bg{16, 24, 40};     /* statusBar.background */

    /* Chrome */
    wxColour border{40, 120, 200};      /* panel/sideBar.border */
    wxColour focus{40, 120, 200};       /* focusBorder */

    /* Text roles */
    wxColour editor_fg{255, 255, 255};  /* editor.foreground */
    wxColour muted_fg{160, 180, 200};   /* descriptionForeground */
    wxColour title_fg{255, 255, 255};   /* titleBar.activeForeground */
    wxColour sidebar_fg{200, 210, 220}; /* sideBar.foreground */
    wxColour status_fg{160, 180, 200};  /* statusBar.foreground */

    /* Controls */
    wxColour button_bg{40, 120, 200};
    wxColour button_fg{255, 255, 255};
    wxColour button_border{40, 120, 200};
    wxColour input_bg{30, 45, 70};
    wxColour input_fg{255, 255, 255};
    wxColour input_border{60, 90, 130};
    wxColour dropdown_bg{28, 40, 58};
    wxColour dropdown_fg{255, 255, 255};
    wxColour bar_track{30, 45, 70};
    wxColour bar_fill{60, 150, 220};
    wxColour selection_bg{40, 120, 200};
    wxColour selection_fg{255, 255, 255};
    wxColour accent{40, 120, 200};
    wxColour highlight{240, 170, 40}; /* active row / cursor accent */

    /* Menu / popup */
    wxColour menu_bg{22, 32, 48};
    wxColour menu_fg{230, 235, 245};
    wxColour menu_sel_bg{40, 120, 200};
    wxColour menu_sel_fg{255, 255, 255};
    wxColour menu_header_fg{160, 180, 200};
    wxColour menu_sep{50, 70, 100};

    wxColour icon{255, 255, 255};
    bool dark = true;
};

enum class OsdTypeRole {
    Title,
    Button,
    Label,
    Value,
    Enum,
    Sidebar,
    Menu,
    MenuHeader,
    Status,
};

wxFont osd_typeface(OsdTypeRole role);

struct ThemeEntry {
    std::string path;
    std::string name;
    std::string group; /* minor | vibe | country */
    std::string type;  /* light | dark */
};

std::vector<std::string> theme_search_roots();
std::vector<ThemeEntry> theme_scan();
bool theme_load(const std::string& path, OsdPalette& out, std::string* name = nullptr);
OsdPalette theme_default_dark();

#endif
