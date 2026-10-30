/*
 * Copyright (C) 2026 Lenik <hdmiosd@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "theme.hpp"

#include <nlohmann/json.hpp>

#include <wx/fontenum.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

OsdPalette theme_default_dark() { return {}; }

static wxString pick_face(std::initializer_list<const char*> candidates) {
    for (const char* name : candidates) {
        if (name && wxFontEnumerator::IsValidFacename(name)) {
            return wxString::FromUTF8(name);
        }
    }
    return {};
}

wxFont osd_typeface(OsdTypeRole role) {
    static wxString sans = pick_face({"DejaVu Sans", "FreeSans", "IBM Plex Sans", "Noto Sans"});
    static wxString sans_cond =
        pick_face({"IBM Plex Sans Condensed", "DejaVu Sans Condensed", "DejaVu Sans", "FreeSans"});
    static wxString mono =
        pick_face({"DejaVu Sans Mono", "IBM Plex Mono", "Fantasque Sans Mono", "Fira Code"});

    auto make = [](int pt, const wxString& face, bool bold, bool italic = false) {
        wxFont f(pt, face.empty() ? wxFONTFAMILY_SWISS : wxFONTFAMILY_DEFAULT, italic ? wxFONTSTYLE_ITALIC : wxFONTSTYLE_NORMAL,
                 bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL, false, face);
        if (!f.IsOk()) {
            f = wxFont(pt, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                       bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
        }
        return f;
    };

    switch (role) {
    case OsdTypeRole::Title:
        return make(12, sans, true);
    case OsdTypeRole::Button:
        return make(9, sans_cond.IsEmpty() ? sans : sans_cond, true);
    case OsdTypeRole::Label:
        return make(10, sans, false);
    case OsdTypeRole::Value:
        return make(10, mono.IsEmpty() ? sans : mono, true);
    case OsdTypeRole::Enum:
        return make(10, mono.IsEmpty() ? sans : mono, true);
    case OsdTypeRole::Sidebar:
        return make(10, sans, true);
    case OsdTypeRole::Menu:
        return make(10, sans, false);
    case OsdTypeRole::MenuHeader:
        return make(9, sans, true, true);
    case OsdTypeRole::Status:
        return make(8, sans, false);
    }
    return make(10, sans, false);
}

static wxColour parse_hex_color(const std::string& s, const wxColour& fallback) {
    if (s.empty() || s[0] != '#') {
        return fallback;
    }
    unsigned r = 0, g = 0, b = 0;
    if (s.size() >= 7 && sscanf(s.c_str() + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
        return wxColour((unsigned char)r, (unsigned char)g, (unsigned char)b);
    }
    return fallback;
}

static std::string color_str(const nlohmann::json& colors, const char* key) {
    if (!colors.is_object() || !colors.contains(key) || !colors[key].is_string()) {
        return {};
    }
    return colors[key].get<std::string>();
}

std::vector<std::string> theme_search_roots() {
    std::vector<std::string> roots;
    if (const char* env = getenv("MINOR_THEMES_DIR")) {
        roots.emplace_back(env);
    }
    roots.emplace_back("/usr/share/minor-themes/themes");
    if (const char* home = getenv("HOME")) {
        roots.push_back(std::string(home) + "/.local/share/minor-themes/themes");
    }
    roots.emplace_back("/home/cursor/archive/minor-themes/themes");
    return roots;
}

std::vector<ThemeEntry> theme_scan() {
    std::vector<ThemeEntry> out;
    const char* groups[] = {"minor", "vibe", "country"};
    for (const auto& root : theme_search_roots()) {
        if (!fs::is_directory(root)) {
            continue;
        }
        for (const char* g : groups) {
            fs::path dir = fs::path(root) / g;
            if (!fs::is_directory(dir)) {
                continue;
            }
            for (const auto& ent : fs::directory_iterator(dir)) {
                if (!ent.is_regular_file()) {
                    continue;
                }
                auto p = ent.path();
                if (p.extension() != ".json") {
                    continue;
                }
                std::ifstream in(p);
                if (!in) {
                    continue;
                }
                nlohmann::json j;
                try {
                    in >> j;
                } catch (...) {
                    continue;
                }
                ThemeEntry te;
                te.path = p.string();
                te.group = g;
                te.name = j.value("name", p.stem().string());
                te.type = j.value("type", "dark");
                if (te.type != "light" && te.type != "dark") {
                    te.type = "dark";
                }
                out.push_back(std::move(te));
            }
        }
        if (!out.empty()) {
            break;
        }
    }
    auto gord = [](const std::string& g) {
        if (g == "minor") {
            return 0;
        }
        if (g == "vibe") {
            return 1;
        }
        return 2;
    };
    std::sort(out.begin(), out.end(), [&](const ThemeEntry& a, const ThemeEntry& b) {
        if (a.group != b.group) {
            return gord(a.group) < gord(b.group);
        }
        if (a.type != b.type) {
            return a.type == "light";
        }
        return a.name < b.name;
    });
    return out;
}

static int colour_luma(const wxColour& c) {
    return (c.Red() * 299 + c.Green() * 587 + c.Blue() * 114) / 1000;
}

static bool colours_too_close(const wxColour& a, const wxColour& b, int min_delta = 72) {
    return std::abs(colour_luma(a) - colour_luma(b)) < min_delta;
}

static wxColour contrast_on(const wxColour& bg) {
    return colour_luma(bg) < 140 ? wxColour(255, 255, 255) : wxColour(20, 20, 20);
}

bool theme_load(const std::string& path, OsdPalette& out, std::string* name) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    nlohmann::json j;
    try {
        in >> j;
    } catch (...) {
        return false;
    }
    if (name) {
        *name = j.value("name", path);
    }
    const auto& colors = j.contains("colors") ? j["colors"] : nlohmann::json::object();
    out = theme_default_dark();
    out.dark = j.value("type", "dark") != "light";

    auto pick_first = [&](wxColour& dest, std::initializer_list<const char*> keys) {
        for (const char* key : keys) {
            auto s = color_str(colors, key);
            if (!s.empty()) {
                dest = parse_hex_color(s, dest);
                return;
            }
        }
    };

    pick_first(out.editor_bg, {"editor.background"});
    pick_first(out.panel_bg, {"titleBar.activeBackground", "sideBarSectionHeader.background",
                              "tab.activeBackground", "sideBar.background"});
    pick_first(out.sidebar_bg, {"sideBar.background", "activityBar.background", "editor.background"});
    pick_first(out.status_bg, {"statusBar.background", "titleBar.inactiveBackground", "sideBar.background"});

    pick_first(out.border, {"panel.border", "sideBar.border", "tab.border", "focusBorder"});
    pick_first(out.focus, {"focusBorder", "activityBar.activeBorder", "button.background"});

    pick_first(out.editor_fg, {"editor.foreground", "foreground"});
    pick_first(out.muted_fg, {"descriptionForeground", "editorLineNumber.foreground",
                              "tab.inactiveForeground"});
    pick_first(out.title_fg, {"titleBar.activeForeground", "editor.foreground", "foreground"});
    pick_first(out.sidebar_fg, {"sideBar.foreground", "sideBarTitle.foreground", "foreground"});
    pick_first(out.status_fg, {"statusBar.foreground", "descriptionForeground", "foreground"});

    pick_first(out.button_bg, {"button.background", "activityBarBadge.background", "focusBorder"});
    pick_first(out.button_fg, {"button.foreground", "activityBarBadge.foreground"});
    if (color_str(colors, "button.foreground").empty() &&
        color_str(colors, "activityBarBadge.foreground").empty()) {
        out.button_fg = contrast_on(out.button_bg);
    }
    pick_first(out.button_border, {"button.border", "focusBorder", "panel.border"});

    pick_first(out.input_bg, {"input.background", "editorWidget.background"});
    pick_first(out.input_fg, {"input.foreground", "editor.foreground"});
    pick_first(out.input_border, {"input.border", "inputOption.activeBorder", "focusBorder"});

    pick_first(out.dropdown_bg, {"dropdown.background", "input.background", "editorWidget.background"});
    pick_first(out.dropdown_fg, {"dropdown.foreground", "input.foreground", "editor.foreground"});

    pick_first(out.bar_track, {"input.background", "editorWidget.background", "sideBar.background"});
    pick_first(out.bar_fill, {"progressBar.background", "textLink.foreground", "terminal.ansiBlue",
                              "button.background"});

    pick_first(out.selection_bg, {"list.activeSelectionBackground", "editor.selectionBackground",
                                  "button.background"});
    pick_first(out.selection_fg, {"list.activeSelectionForeground", "editor.foreground"});
    if (color_str(colors, "list.activeSelectionForeground").empty()) {
        out.selection_fg = contrast_on(out.selection_bg);
    }

    pick_first(out.accent, {"activityBarBadge.background", "focusBorder", "button.background"});
    pick_first(out.highlight, {"editorCursor.foreground", "terminal.ansiYellow",
                               "list.highlightForeground", "activityBarBadge.background"});

    pick_first(out.menu_bg, {"menu.background", "dropdown.background", "editorWidget.background",
                             "sideBar.background"});
    pick_first(out.menu_fg, {"menu.foreground", "dropdown.foreground", "foreground"});
    pick_first(out.menu_sel_bg, {"menu.selectionBackground", "list.activeSelectionBackground",
                                 "button.background"});
    pick_first(out.menu_sel_fg, {"menu.selectionForeground", "list.activeSelectionForeground"});
    if (color_str(colors, "menu.selectionForeground").empty() &&
        color_str(colors, "list.activeSelectionForeground").empty()) {
        out.menu_sel_fg = contrast_on(out.menu_sel_bg);
    }
    pick_first(out.menu_header_fg, {"menu.separatorBackground", "descriptionForeground",
                                    "sideBarTitle.foreground"});
    pick_first(out.menu_sep, {"menu.separatorBackground", "panel.border", "sideBar.border"});

    /* Contrast guards */
    if (colours_too_close(out.editor_fg, out.editor_bg)) {
        out.editor_fg = contrast_on(out.editor_bg);
    }
    if (colours_too_close(out.title_fg, out.panel_bg)) {
        out.title_fg = contrast_on(out.panel_bg);
    }
    if (colours_too_close(out.sidebar_fg, out.sidebar_bg)) {
        out.sidebar_fg = contrast_on(out.sidebar_bg);
    }
    if (colours_too_close(out.button_fg, out.button_bg)) {
        out.button_fg = contrast_on(out.button_bg);
    }
    if (colours_too_close(out.selection_fg, out.selection_bg)) {
        out.selection_fg = contrast_on(out.selection_bg);
    }
    if (colours_too_close(out.highlight, out.selection_bg)) {
        out.highlight = out.dark ? wxColour(240, 180, 50) : wxColour(180, 90, 20);
    }
    if (colours_too_close(out.bar_fill, out.bar_track)) {
        out.bar_fill = out.accent;
    }
    if (colours_too_close(out.menu_fg, out.menu_bg)) {
        out.menu_fg = contrast_on(out.menu_bg);
    }
    if (colours_too_close(out.menu_sel_fg, out.menu_sel_bg)) {
        out.menu_sel_fg = contrast_on(out.menu_sel_bg);
    }
    out.icon = contrast_on(out.editor_bg);
    out.focus = out.focus.IsOk() ? out.focus : out.accent;
    return true;
}
