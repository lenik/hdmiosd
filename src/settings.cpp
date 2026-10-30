/*
 * Copyright (C) 2026 Lenik <hdmiosd@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "settings.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

static void ensure_dir(const std::string& path) {
    mkdir(path.c_str(), 0755);
}

std::string Settings::config_path() {
    const char* home = getenv("HOME");
    std::string base = home ? home : ".";
    std::string dir = base + "/.config/hdmiosd";
    ensure_dir(base + "/.config");
    ensure_dir(dir);
    return dir + "/settings.ini";
}

std::string Settings::shortcut_for(const std::string& action, const std::string& def) const {
    auto it = shortcuts.find(action);
    return it == shortcuts.end() ? def : it->second;
}

void Settings::set_shortcut(const std::string& action, const std::string& key) {
    shortcuts[action] = key;
}

void Settings::load() {
    shortcuts.clear();
    /* defaults */
    shortcuts["brightness"] = "b";
    shortcuts["contrast"] = "c";
    shortcuts["sharpness"] = "s";
    shortcuts["color_preset"] = "t";
    shortcuts["volume"] = "v";
    shortcuts["balance"] = "a";
    shortcuts["input"] = "i";
    shortcuts["power"] = "p";
    shortcuts["factory"] = "0";
    shortcuts["refresh"] = "F5";
    shortcuts["refresh_displays"] = "F6";
    shortcuts["undo"] = "Ctrl+Z";
    shortcuts["reset"] = "Ctrl+R";

    std::ifstream in(config_path());
    if (!in) {
        return;
    }
    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        auto trim = [](std::string& s) {
            while (!s.empty() && s.front() == ' ') {
                s.erase(s.begin());
            }
            while (!s.empty() && s.back() == ' ') {
                s.pop_back();
            }
        };
        trim(key);
        trim(val);
        if (section == "display") {
            if (key == "last") {
                last_display = val;
            }
        } else if (section == "app_osd") {
            if (key == "opacity") {
                app.opacity = std::strtod(val.c_str(), nullptr) / 100.0;
            } else if (key == "width") {
                app.width = std::atoi(val.c_str());
            } else if (key == "height") {
                app.height = std::atoi(val.c_str());
            } else if (key == "margin_left") {
                app.margin_left = std::atoi(val.c_str());
            } else if (key == "margin_right") {
                app.margin_right = std::atoi(val.c_str());
            } else if (key == "margin_top") {
                app.margin_top = std::atoi(val.c_str());
            } else if (key == "margin_bottom") {
                app.margin_bottom = std::atoi(val.c_str());
            } else if (key == "have_left") {
                app.have_left = val == "1" || val == "true";
            } else if (key == "have_right") {
                app.have_right = val == "1" || val == "true";
            } else if (key == "have_top") {
                app.have_top = val == "1" || val == "true";
            } else if (key == "have_bottom") {
                app.have_bottom = val == "1" || val == "true";
            } else if (key == "theme") {
                app.theme = val;
            }
        } else if (section == "shortcuts") {
            shortcuts[key] = val;
        }
    }
}

void Settings::save() const {
    std::ofstream out(config_path());
    if (!out) {
        return;
    }
    out << "# hdmiosd settings\n\n";
    out << "[display]\n";
    out << "last=" << last_display << "\n\n";
    out << "[app_osd]\n";
    out << "opacity=" << (int)(app.opacity * 100 + 0.5) << "\n";
    out << "width=" << app.width << "\n";
    out << "height=" << app.height << "\n";
    out << "margin_left=" << app.margin_left << "\n";
    out << "margin_right=" << app.margin_right << "\n";
    out << "margin_top=" << app.margin_top << "\n";
    out << "margin_bottom=" << app.margin_bottom << "\n";
    out << "have_left=" << (app.have_left ? 1 : 0) << "\n";
    out << "have_right=" << (app.have_right ? 1 : 0) << "\n";
    out << "have_top=" << (app.have_top ? 1 : 0) << "\n";
    out << "have_bottom=" << (app.have_bottom ? 1 : 0) << "\n";
    out << "theme=" << app.theme << "\n\n";
    out << "[shortcuts]\n";
    for (const auto& kv : shortcuts) {
        out << kv.first << "=" << kv.second << "\n";
    }
}
