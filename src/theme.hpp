#ifndef HDMIOOSD_THEME_HPP
#define HDMIOOSD_THEME_HPP

#include <wx/colour.h>
#include <wx/string.h>

#include <string>
#include <vector>

struct OsdPalette {
    wxColour bg{14, 22, 36};
    wxColour panel{18, 30, 48};
    wxColour side{20, 34, 52};
    wxColour accent{40, 120, 200};
    wxColour active{240, 170, 40};
    wxColour text{255, 255, 255};
    wxColour muted{160, 180, 200};
    wxColour bar_bg{30, 45, 70};
    wxColour bar{60, 150, 220};
    wxColour border{40, 120, 200};
    wxColour icon{255, 255, 255}; /* tint for Tabler icons */
    bool dark = true;
};

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
