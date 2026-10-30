#ifndef HDMIOOSD_OSD_FRAME_HPP
#define HDMIOOSD_OSD_FRAME_HPP

#include "display.hpp"
#include "settings.hpp"
#include "theme.hpp"

#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/timer.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

struct HistStack {
    std::vector<uint16_t> vals;
    void push(uint16_t v) { vals.push_back(v); }
    bool pop(uint16_t& out) {
        if (vals.empty()) {
            return false;
        }
        out = vals.back();
        vals.pop_back();
        return true;
    }
    void clear() { vals.clear(); }
};

struct OsdItem {
    const VcpFeature* feat = nullptr;
    VcpKind kind = VcpKind::Continuous;
    uint8_t code = 0;
    uint16_t current = 0;
    uint16_t max = 0;
    uint16_t baseline = 0;
    std::vector<uint16_t> choices;
    std::vector<std::string> choice_labels; /* English msgids or dynamic */
    int choice_idx = -1;
    HistStack hist;
    int app_id = -1;
    double row_y = 0;
    double bar_x0 = 0, bar_x1 = 0, bar_y0 = 0, bar_y1 = 0;
    double undo_x0 = 0, undo_x1 = 0, reset_x0 = 0, reset_x1 = 0;
    double btn_x0 = 0, btn_x1 = 0;
    double left_x0 = 0, left_x1 = 0, right_x0 = 0, right_x1 = 0; /* discrete < > */
};

class OsdCanvas;

class OsdFrame : public wxFrame {
  public:
    OsdFrame(DisplayList* list, Settings* settings, const std::string& device_filter);
    ~OsdFrame() override;

    void rebuild_items(); /* start/restart async feature query for current display */
    void refresh_displays();
    void refresh_all(); /* rediscover displays + re-read controls */
    void apply_window_placement();
    void save_settings();
    void apply_theme(const std::string& path);
    const OsdPalette& palette() const { return palette_; }
    void popup_theme_menu();

    DisplayList* list() { return list_; }
    Settings* settings() { return settings_; }
    int current_display() const { return current_display_; }
    void set_current_display(int i);

    std::vector<OsdItem>& items() { return items_; }
    const std::vector<int>& visible_cats() const { return visible_cats_; }
    int cat_index() const { return cat_index_; }
    void set_cat_index(int i);
    int active_item() const { return active_item_; }
    void set_active_item(int i) { active_item_ = i; }
    uint16_t color_temp_step() const { return color_temp_step_; }
    const std::string& status() const { return status_; }
    void set_status(const std::string& s);
    bool loading() const { return loading_; }

    void queue_set_item(int idx, uint16_t value, bool debounce, bool record_hist);
    void undo_item(int idx);
    void reset_item(int idx);
    void apply_button(int idx);
    void adjust_active(int delta);
    bool activate_shortcut(const std::string& key);
    void refresh_item_values(const uint8_t* codes, bool all);

    const std::string& device_filter() const { return device_filter_; }
    OsdCanvas* canvas() { return canvas_; }

  private:
    DisplayList* list_;
    Settings* settings_;
    std::string device_filter_;
    int current_display_ = 0;
    std::vector<OsdItem> items_;
    std::vector<int> visible_cats_;
    int cat_index_ = 0;
    int active_item_ = -1;
    uint16_t color_temp_step_ = 100;
    std::string status_;
    OsdCanvas* canvas_ = nullptr;
    wxTimer debounce_;
    int pending_item_ = -1;
    uint16_t pending_value_ = 0;
    OsdPalette palette_;

    std::shared_ptr<std::atomic<bool>> alive_;
    std::atomic<uint64_t> load_gen_{0};
    std::thread worker_;
    bool loading_ = false;

    void on_debounce(wxTimerEvent&);
    void flush_pending();
    void build_app_osd_items();
    void show_shell_ui(const std::string& status_msg);
    void rebuild_visible_cats();
    void cancel_background();
    void start_async_scan();
    void start_async_feature_query();
    void ui_on_display_found(HdmiDisplay&& d, uint64_t gen);
    void ui_on_scan_done(uint64_t gen, size_t n);
    void ui_on_feature_ready(OsdItem&& it, uint64_t gen);
    void ui_on_features_done(uint64_t gen);
    bool make_item_from_feat(HdmiDisplay& d, const VcpFeature& feat, OsdItem& it);

    wxDECLARE_EVENT_TABLE();
};

class OsdCanvas : public wxPanel {
  public:
    explicit OsdCanvas(OsdFrame* frame);

  private:
    OsdFrame* frame_;
    int drag_item_ = -1;
    bool drag_hist_pushed_ = false;
    bool win_drag_ = false;
    wxPoint win_drag_origin_;
    wxPoint win_drag_start_;

    void on_paint(wxPaintEvent&);
    void on_left_down(wxMouseEvent&);
    void on_left_up(wxMouseEvent&);
    void on_motion(wxMouseEvent&);
    void on_mousewheel(wxMouseEvent&);
    void on_key_down(wxKeyEvent&);
    void on_char_hook(wxKeyEvent&);

    int hit_sidebar(int x, int y) const;
    int hit_item(int x, int y, int* zone) const;
    int hit_top_btn(int x, int y) const; /* 0=refresh 1=theme */

    wxDECLARE_EVENT_TABLE();
};

#endif
