#ifndef HDMIOOSD_SETTINGS_HPP
#define HDMIOOSD_SETTINGS_HPP

#include <map>
#include <string>

struct AppOsdSettings {
    double opacity = 0.95; /* 0..1 */
    int width = 640;
    int height = 480;
    int margin_left = 0;
    int margin_right = 0;
    int margin_top = 0;
    int margin_bottom = 0;
    bool have_left = false;
    bool have_right = false;
    bool have_top = false;
    bool have_bottom = false; /* all false => centered on display */
    std::string theme;        /* absolute path to color-theme.json */
};

struct Settings {
    std::string last_display; /* i2c path or connector */
    AppOsdSettings app;
    /* action id -> key name, e.g. "brightness" -> "b", "refresh" -> "F5" */
    std::map<std::string, std::string> shortcuts;

    static std::string config_path();
    void load();
    void save() const;

    std::string shortcut_for(const std::string& action, const std::string& def) const;
    void set_shortcut(const std::string& action, const std::string& key);
};

#endif
