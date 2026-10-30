/*
 * Copyright (C) 2026 Lenik <hdmiosd@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "theme.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

OsdPalette theme_default_dark() { return {}; }

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
    /* Dev / local archive fallback */
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
            break; /* first existing root wins */
        }
    }
    /* Sort: group order minor/vibe/country, then light before dark, then name */
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
            return a.type == "light"; /* light first */
        }
        return a.name < b.name;
    });
    return out;
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

    auto pick = [&](const char* key, wxColour& dest) {
        auto s = color_str(colors, key);
        if (!s.empty()) {
            dest = parse_hex_color(s, dest);
        }
    };

    pick("editor.background", out.bg);
    pick("sideBar.background", out.side);
    pick("titleBar.activeBackground", out.panel);
    if (color_str(colors, "titleBar.activeBackground").empty()) {
        out.panel = out.side;
    }
    pick("foreground", out.text);
    pick("editor.foreground", out.text);
    pick("descriptionForeground", out.muted);
    pick("editorLineNumber.foreground", out.muted);
    pick("button.background", out.accent);
    pick("focusBorder", out.border);
    pick("button.background", out.active);
    pick("activityBarBadge.background", out.active);
    pick("input.background", out.bar_bg);
    pick("button.background", out.bar);
    out.icon = out.text;
    /* Ensure icons stay readable: prefer white on dark, near-black on light */
    if (out.dark) {
        out.icon = wxColour(255, 255, 255);
    } else {
        out.icon = wxColour(30, 30, 30);
    }
    return true;
}
