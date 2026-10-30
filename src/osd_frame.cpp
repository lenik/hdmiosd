/*
 * Copyright (C) 2026 Lenik <hdmiosd@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "osd_frame.hpp"

#include "config.h"
#include "icons.hpp"

#include <bas/locale/i18n.h>

#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/menu.h>
#include <wx/sizer.h>

#include <algorithm>
#include <cmath>
#include <unistd.h>

namespace {
constexpr int SIDEBAR_W = 160;
constexpr int ROW_H = 36;
constexpr int TOP_H = 48;
constexpr int BTN_W = 28;
constexpr int PAD = 10;
constexpr int ARROW_W = 22;

enum AppCtrlId {
    APP_OPACITY = 0,
    APP_WIDTH,
    APP_HEIGHT,
    APP_LEFT,
    APP_RIGHT,
    APP_TOP,
    APP_BOTTOM,
    APP_COUNT
};

wxString tr(const char* msgid) { return wxString::FromUTF8(_(msgid)); }
wxString tr(const std::string& msgid) { return wxString::FromUTF8(_(msgid.c_str())); }

void draw_round_rect(wxDC& dc, int x, int y, int w, int h, int r, const wxColour& fill,
                     const wxColour* stroke = nullptr) {
    dc.SetBrush(wxBrush(fill));
    if (stroke) {
        dc.SetPen(wxPen(*stroke, 1));
    } else {
        dc.SetPen(*wxTRANSPARENT_PEN);
    }
    dc.DrawRoundedRectangle(x, y, w, h, r);
}

int text_vcenter_y(wxDC& dc, int row_y, int row_h) {
    wxCoord tw = 0, th = 0;
    dc.GetTextExtent("Ag", &tw, &th);
    return row_y + (row_h - th) / 2;
}
} // namespace

wxBEGIN_EVENT_TABLE(OsdFrame, wxFrame)
    EVT_TIMER(wxID_ANY, OsdFrame::on_debounce)
wxEND_EVENT_TABLE()

OsdFrame::OsdFrame(DisplayList* list, Settings* settings, const std::string& device_filter)
    : wxFrame(nullptr, wxID_ANY, "hdmiosd", wxDefaultPosition, wxSize(640, 480),
              wxBORDER_NONE | wxSTAY_ON_TOP | wxFRAME_TOOL_WINDOW),
      list_(list), settings_(settings), device_filter_(device_filter),
      alive_(std::make_shared<std::atomic<bool>>(true)) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    palette_ = theme_default_dark();
    if (settings_ && !settings_->app.theme.empty()) {
        theme_load(settings_->app.theme, palette_);
    }
    wxBitmap app_ic = load_app_icon(64);
    if (app_ic.IsOk()) {
        wxIcon icon;
        icon.CopyFromBitmap(app_ic);
        SetIcon(icon);
    }
    canvas_ = new OsdCanvas(this);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(canvas_, 1, wxEXPAND);
    SetSizer(sizer);
    debounce_.SetOwner(this);
    apply_window_placement();
    show_shell_ui(_("Scanning displays…"));
    start_async_scan();
    canvas_->SetFocus();
}

OsdFrame::~OsdFrame() {
    cancel_background();
}

void OsdFrame::set_status(const std::string& s) {
    status_ = s;
    if (canvas_) {
        canvas_->Refresh(false);
    }
}

void OsdFrame::set_current_display(int i) {
    if (!list_ || list_->size() == 0) {
        return;
    }
    if (i < 0 || (size_t)i >= list_->size()) {
        return;
    }
    if (current_display_ >= 0 && (size_t)current_display_ < list_->size() &&
        current_display_ != i) {
        list_->at(current_display_).close();
    }
    current_display_ = i;
    if (settings_) {
        settings_->last_display = list_->at(current_display_).i2c_path;
        settings_->save();
    }
    rebuild_items();
}

void OsdFrame::set_cat_index(int i) {
    if (i < 0 || i >= (int)visible_cats_.size()) {
        return;
    }
    cat_index_ = i;
    active_item_ = -1;
    auto cat = (VcpCategory)visible_cats_[cat_index_];
    for (int j = 0; j < (int)items_.size(); j++) {
        VcpCategory c = items_[j].feat ? items_[j].feat->cat : VcpCategory::AppOsd;
        if (c == cat && items_[j].kind != VcpKind::Ro) {
            active_item_ = j;
            break;
        }
    }
    if (canvas_) {
        canvas_->Refresh(false);
    }
}

void OsdFrame::apply_window_placement() {
    auto& a = settings_->app;
    SetClientSize(a.width > 0 ? a.width : 640, a.height > 0 ? a.height : 480);
    SetTransparent((wxByte)std::clamp((int)(a.opacity * 255), 30, 255));

    /* Prefer the display under the mouse (same screen the user is on). */
    int disp_idx = wxDisplay::GetFromPoint(wxGetMousePosition());
    if (disp_idx == wxNOT_FOUND) {
        disp_idx = wxDisplay::GetFromWindow(this);
    }
    if (disp_idx == wxNOT_FOUND) {
        disp_idx = 0;
    }
    wxDisplay disp((unsigned)disp_idx);
    wxRect geo = disp.GetClientArea();
    if (geo.width <= 0) {
        geo = disp.GetGeometry();
    }
    int w, h;
    GetSize(&w, &h);
    int x = geo.x + (geo.width - w) / 2;
    int y = geo.y + (geo.height - h) / 2;
    if (a.have_left) {
        x = geo.x + a.margin_left;
    } else if (a.have_right) {
        x = geo.x + geo.width - w - a.margin_right;
    }
    if (a.have_top) {
        y = geo.y + a.margin_top;
    } else if (a.have_bottom) {
        y = geo.y + geo.height - h - a.margin_bottom;
    }
    Move(x, y);
}

void OsdFrame::apply_theme(const std::string& path) {
    OsdPalette pal;
    if (!theme_load(path, pal)) {
        set_status(_("Failed to load theme"));
        return;
    }
    palette_ = pal;
    if (settings_) {
        settings_->app.theme = path;
        settings_->save();
    }
    if (canvas_) {
        canvas_->Refresh(false);
    }
    set_status(_("Theme applied"));
}

void OsdFrame::popup_theme_menu() {
    auto themes = theme_scan();
    if (themes.empty()) {
        set_status(_("No minor-themes found (set MINOR_THEMES_DIR)"));
        return;
    }
    wxMenu menu;
    wxMenu* sub_minor = new wxMenu;
    wxMenu* sub_vibe = new wxMenu;
    wxMenu* sub_country = new wxMenu;
    int id_base = 8000;
    std::vector<std::string> id_paths;
    auto add_group = [&](wxMenu* sub, const std::string& group) {
        bool any_light = false, any_dark = false;
        for (const auto& t : themes) {
            if (t.group == group && t.type == "light") {
                any_light = true;
            }
            if (t.group == group && t.type == "dark") {
                any_dark = true;
            }
        }
        auto add_hdr = [&](const char* label) {
            wxMenuItem* hdr = sub->Append(wxID_ANY, label);
            hdr->Enable(false);
        };
        auto add_type = [&](const char* type) {
            for (const auto& t : themes) {
                if (t.group != group || t.type != type) {
                    continue;
                }
                int id = id_base + (int)id_paths.size();
                id_paths.push_back(t.path);
                sub->Append(id, t.name);
            }
        };
        if (any_light) {
            add_hdr(_("— Light —"));
            add_type("light");
        }
        if (any_light && any_dark) {
            sub->AppendSeparator();
        }
        if (any_dark) {
            add_hdr(_("— Dark —"));
            add_type("dark");
        }
    };
    add_group(sub_minor, "minor");
    add_group(sub_vibe, "vibe");
    add_group(sub_country, "country");
    menu.AppendSubMenu(sub_minor, wxString::FromUTF8(_("Minor")));
    menu.AppendSubMenu(sub_vibe, wxString::FromUTF8(_("Vibe")));
    menu.AppendSubMenu(sub_country, wxString::FromUTF8(_("Country")));

    int cmd = GetPopupMenuSelectionFromUser(menu);
    if (cmd >= id_base && cmd < id_base + (int)id_paths.size()) {
        apply_theme(id_paths[cmd - id_base]);
    }
}

void OsdFrame::save_settings() {
    if (settings_) {
        settings_->save();
    }
}

void OsdFrame::build_app_osd_items() {
    auto add = [&](int id, const char* name, const char* icon, uint16_t cur, uint16_t max) {
        OsdItem it;
        it.app_id = id;
        it.kind = VcpKind::Continuous;
        it.current = cur;
        it.max = max;
        it.baseline = cur;
        it.code = (uint8_t)id;
        it.choice_labels.push_back(name); /* English msgid */
        it.choice_labels.push_back(icon ? icon : "apps");
        items_.push_back(std::move(it));
    };
    auto& a = settings_->app;
    add(APP_OPACITY, N_("Opacity %"), "apps", (uint16_t)(a.opacity * 100 + 0.5), 100);
    add(APP_WIDTH, N_("Width"), "layout-sidebar", (uint16_t)std::clamp(a.width, 200, 4000), 2000);
    add(APP_HEIGHT, N_("Height"), "layout-sidebar", (uint16_t)std::clamp(a.height, 160, 3000),
        1600);
    add(APP_LEFT, N_("Margin left"), "arrow-back", (uint16_t)a.margin_left, 400);
    add(APP_RIGHT, N_("Margin right"), "arrow-forward", (uint16_t)a.margin_right, 400);
    add(APP_TOP, N_("Margin top"), "arrow-back-up", (uint16_t)a.margin_top, 400);
    add(APP_BOTTOM, N_("Margin bottom"), "arrow-back", (uint16_t)a.margin_bottom, 400);
}

void OsdFrame::show_shell_ui(const std::string& status_msg) {
    items_.clear();
    visible_cats_.clear();
    active_item_ = -1;
    color_temp_step_ = 100;
    visible_cats_.push_back((int)VcpCategory::AppOsd);
    cat_index_ = 0;
    build_app_osd_items();
    set_status(status_msg);
    if (canvas_) {
        canvas_->Refresh(false);
    }
}

void OsdFrame::rebuild_visible_cats() {
    bool cat_has[(int)VcpCategory::Count] = {};
    for (const auto& it : items_) {
        if (it.app_id >= 0) {
            cat_has[(int)VcpCategory::AppOsd] = true;
        } else if (it.feat) {
            cat_has[(int)it.feat->cat] = true;
        }
    }
    int prev = cat_index_ < (int)visible_cats_.size() ? visible_cats_[cat_index_] : -1;
    visible_cats_.clear();
    bool app_inserted = false;
    for (int c = 0; c < (int)VcpCategory::AppOsd; c++) {
        if (cat_has[c]) {
            visible_cats_.push_back(c);
        }
        if (c == (int)VcpCategory::DeviceOsd) {
            visible_cats_.push_back((int)VcpCategory::AppOsd);
            app_inserted = true;
        }
    }
    if (!app_inserted) {
        visible_cats_.push_back((int)VcpCategory::AppOsd);
    }
    cat_index_ = 0;
    for (int i = 0; i < (int)visible_cats_.size(); i++) {
        if (visible_cats_[i] == prev) {
            cat_index_ = i;
            break;
        }
    }
}

void OsdFrame::cancel_background() {
    ++load_gen_;
    loading_ = false;
    if (alive_) {
        alive_->store(false);
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    alive_ = std::make_shared<std::atomic<bool>>(true);
}

bool OsdFrame::make_item_from_feat(HdmiDisplay& d, const VcpFeature& feat, OsdItem& it) {
    int s = vcp_caps_supports(d.caps, feat.code);
    if (s == 0) {
        return false;
    }
    it = {};
    it.feat = &feat;
    it.code = feat.code;
    it.kind = feat.kind;
    if (feat.kind == VcpKind::Button) {
        return true;
    }
    if (!d.bus.is_open() && d.bus.open(d.i2c_path) != DdcErr::Ok) {
        return false;
    }
    DdcVcpValue v;
    if (d.bus.get_vcp(feat.code, v) != DdcErr::Ok || !v.valid) {
        return false;
    }
    it.current = v.current;
    it.max = v.max ? v.max : 100;
    it.baseline = it.current;
    if (feat.kind == VcpKind::Discrete) {
        const bool color_temp_req = (feat.code == 0x0c && feat.nchoices > 0);
        if (!color_temp_req && d.caps.nvalues[feat.code] > 0) {
            for (size_t i = 0; i < d.caps.nvalues[feat.code]; i++) {
                uint16_t val = d.caps.values[feat.code][i];
                it.choices.push_back(val);
                const char* lab = nullptr;
                for (size_t j = 0; j < feat.nchoices; j++) {
                    if (feat.choices[j].value == val) {
                        lab = feat.choices[j].label;
                        break;
                    }
                }
                char fb[32];
                if (!lab) {
                    snprintf(fb, sizeof(fb), "0x%02X", val);
                    lab = fb;
                }
                it.choice_labels.push_back(lab);
            }
        } else if (feat.nchoices) {
            for (size_t i = 0; i < feat.nchoices; i++) {
                it.choices.push_back(feat.choices[i].value);
                it.choice_labels.push_back(feat.choices[i].label);
            }
        }
        bool have_cur = false;
        for (uint16_t c : it.choices) {
            if (c == it.current) {
                have_cur = true;
                break;
            }
        }
        if (!have_cur) {
            char fb[40];
            const char* lab =
                feat.code == 0x0c
                    ? vcp_color_temp_mode_label(it.current, color_temp_step_, fb, sizeof(fb))
                    : (snprintf(fb, sizeof(fb), "0x%02X", it.current), fb);
            it.choices.push_back(it.current);
            it.choice_labels.push_back(lab);
        }
        it.choice_idx = 0;
        for (size_t i = 0; i < it.choices.size(); i++) {
            if (it.choices[i] == it.current) {
                it.choice_idx = (int)i;
                break;
            }
        }
    }
    return true;
}

void OsdFrame::ui_on_display_found(HdmiDisplay&& d, uint64_t gen) {
    if (!alive_->load() || gen != load_gen_.load() || !list_) {
        d.close();
        return;
    }
    list_->adopt(std::move(d));
    if (list_->size() == 1) {
        current_display_ = 0;
    }
    if (settings_ && !settings_->last_display.empty()) {
        for (size_t i = 0; i < list_->size(); i++) {
            if (list_->at(i).i2c_path == settings_->last_display ||
                list_->at(i).connector == settings_->last_display) {
                current_display_ = (int)i;
                break;
            }
        }
    }
    char buf[96];
    snprintf(buf, sizeof(buf), _("Found %zu display(s)…"), list_->size());
    set_status(buf);
}

void OsdFrame::ui_on_scan_done(uint64_t gen, size_t n) {
    if (!alive_->load() || gen != load_gen_.load()) {
        return;
    }
    if (n == 0) {
        loading_ = false;
        show_shell_ui(_("No DDC-capable displays found"));
        return;
    }
    if (current_display_ < 0 || (size_t)current_display_ >= list_->size()) {
        current_display_ = 0;
    }
    if (settings_) {
        settings_->last_display = list_->at(current_display_).i2c_path;
        settings_->save();
    }
    start_async_feature_query();
}

void OsdFrame::ui_on_feature_ready(OsdItem&& it, uint64_t gen) {
    if (!alive_->load() || gen != load_gen_.load()) {
        return;
    }
    /* Insert device items before trailing App OSD block. */
    size_t app_start = items_.size();
    for (size_t i = 0; i < items_.size(); i++) {
        if (items_[i].app_id >= 0) {
            app_start = i;
            break;
        }
    }
    items_.insert(items_.begin() + (long)app_start, std::move(it));
    rebuild_visible_cats();
    if (active_item_ < 0) {
        set_cat_index(cat_index_);
    }
    if (canvas_) {
        canvas_->Refresh(false);
    }
}

void OsdFrame::ui_on_features_done(uint64_t gen) {
    if (!alive_->load() || gen != load_gen_.load()) {
        return;
    }
    loading_ = false;
    rebuild_visible_cats();
    if (cat_index_ >= (int)visible_cats_.size()) {
        cat_index_ = 0;
    }
    set_cat_index(cat_index_);
    if (list_ && list_->size()) {
        set_status(list_->at(current_display_).label());
    } else {
        set_status(_("Ready"));
    }
}

void OsdFrame::start_async_scan() {
    cancel_background();
    uint64_t gen = ++load_gen_;
    loading_ = true;
    auto alive = alive_;
    std::string filter = device_filter_;
    worker_ = std::thread([this, alive, gen, filter]() {
        auto cands = DisplayList::enumerate_candidates(filter);
        size_t found = 0;
        for (const auto& c : cands) {
            if (!alive->load() || gen != load_gen_.load()) {
                return;
            }
            HdmiDisplay d;
            if (!DisplayList::probe_candidate(c, d)) {
                continue;
            }
            ++found;
            auto* moved = new HdmiDisplay(std::move(d));
            CallAfter([this, alive, gen, moved]() {
                std::unique_ptr<HdmiDisplay> holder(moved);
                if (!alive->load() || gen != load_gen_.load()) {
                    return;
                }
                ui_on_display_found(std::move(*holder), gen);
            });
        }
        CallAfter([this, alive, gen, found]() {
            if (!alive->load() || gen != load_gen_.load()) {
                return;
            }
            ui_on_scan_done(gen, found);
        });
    });
}

void OsdFrame::start_async_feature_query() {
    if (!list_ || list_->size() == 0) {
        loading_ = false;
        return;
    }
    /* Keep App OSD; drop previous device items. */
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [](const OsdItem& it) { return it.app_id < 0; }),
                 items_.end());
    if (items_.empty()) {
        build_app_osd_items();
    }
    rebuild_visible_cats();
    set_status(_("Reading controls…"));

    uint64_t gen = load_gen_.load();
    loading_ = true;
    auto alive = alive_;
    int disp = current_display_;
    std::string path = list_->at(disp).i2c_path;
    VcpCaps caps = list_->at(disp).caps;

    if (worker_.joinable()) {
        worker_.join();
    }
    worker_ = std::thread([this, alive, gen, path, caps, disp]() mutable {
        HdmiDisplay probe;
        probe.i2c_path = path;
        probe.caps = caps;
        if (probe.bus.open(path) != DdcErr::Ok) {
            CallAfter([this, alive, gen]() {
                if (!alive->load() || gen != load_gen_.load()) {
                    return;
                }
                loading_ = false;
                set_status(_("Cannot open display"));
            });
            return;
        }
        DdcVcpValue step;
        uint16_t step_val = 100;
        if (probe.bus.get_vcp(0x0b, step) == DdcErr::Ok && step.valid && step.current) {
            step_val = step.current;
        }
        CallAfter([this, alive, gen, step_val]() {
            if (!alive->load() || gen != load_gen_.load()) {
                return;
            }
            color_temp_step_ = step_val;
        });

        for (const auto& feat : vcp_features()) {
            if (!alive->load() || gen != load_gen_.load()) {
                probe.bus.close();
                return;
            }
            OsdItem it;
            if (!make_item_from_feat(probe, feat, it)) {
                continue;
            }
            auto* heap = new OsdItem(std::move(it));
            CallAfter([this, alive, gen, heap]() {
                std::unique_ptr<OsdItem> holder(heap);
                if (!alive->load() || gen != load_gen_.load()) {
                    return;
                }
                ui_on_feature_ready(std::move(*holder), gen);
            });
        }
        probe.bus.close();
        CallAfter([this, alive, gen]() {
            if (!alive->load() || gen != load_gen_.load()) {
                return;
            }
            ui_on_features_done(gen);
        });
        (void)disp;
    });
}

void OsdFrame::rebuild_items() {
    if (!list_ || list_->size() == 0) {
        show_shell_ui(_("No display"));
        return;
    }
    start_async_feature_query();
}

void OsdFrame::refresh_displays() { refresh_all(); }

void OsdFrame::refresh_all() {
    cancel_background();
    if (list_) {
        list_->clear();
    }
    current_display_ = 0;
    show_shell_ui(_("Scanning displays…"));
    start_async_scan();
}

void OsdFrame::on_debounce(wxTimerEvent&) { flush_pending(); }

void OsdFrame::flush_pending() {
    if (pending_item_ < 0 || pending_item_ >= (int)items_.size()) {
        return;
    }
    OsdItem& it = items_[pending_item_];
    if (it.app_id >= 0) {
        auto& a = settings_->app;
        switch (it.app_id) {
        case APP_OPACITY:
            a.opacity = pending_value_ / 100.0;
            SetTransparent((wxByte)std::clamp((int)(a.opacity * 255), 30, 255));
            break;
        case APP_WIDTH:
            a.width = pending_value_;
            SetClientSize(a.width, a.height);
            break;
        case APP_HEIGHT:
            a.height = pending_value_;
            SetClientSize(a.width, a.height);
            break;
        case APP_LEFT:
            a.margin_left = pending_value_;
            a.have_left = true;
            a.have_right = false;
            apply_window_placement();
            break;
        case APP_RIGHT:
            a.margin_right = pending_value_;
            a.have_right = true;
            a.have_left = false;
            apply_window_placement();
            break;
        case APP_TOP:
            a.margin_top = pending_value_;
            a.have_top = true;
            a.have_bottom = false;
            apply_window_placement();
            break;
        case APP_BOTTOM:
            a.margin_bottom = pending_value_;
            a.have_bottom = true;
            a.have_top = false;
            apply_window_placement();
            break;
        }
        it.current = pending_value_;
        settings_->save();
        set_status(_("App OSD updated"));
        canvas_->Refresh(false);
        return;
    }

    if (!list_ || list_->size() == 0) {
        return;
    }
    HdmiDisplay& d = list_->at(current_display_);
    if (!d.open()) {
        set_status(_("Cannot open display"));
        return;
    }
    if (it.code == 0x16 || it.code == 0x18 || it.code == 0x1a) {
        DdcVcpValue preset;
        if (d.bus.get_vcp(0x14, preset) == DdcErr::Ok && preset.valid) {
            if (preset.current != 0x0a && preset.current != 0x0b) {
                d.bus.set_vcp(0x14, 0x0b);
                usleep(50000);
            }
        }
    }
    auto rc = d.bus.set_vcp(it.code, pending_value_);
    if (rc != DdcErr::Ok) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Set 0x%02X failed: %s", it.code, DdcBus::strerror(rc));
        set_status(buf);
    } else {
        usleep(40000);
        DdcVcpValue v;
        if (d.bus.get_vcp(it.code, v) == DdcErr::Ok && v.valid) {
            it.current = v.current;
        } else {
            it.current = pending_value_;
        }
        char buf[96];
        snprintf(buf, sizeof(buf), "Set 0x%02X = %u", it.code, (unsigned)it.current);
        set_status(buf);
    }
    canvas_->Refresh(false);
}

void OsdFrame::queue_set_item(int idx, uint16_t value, bool debounce, bool record_hist) {
    if (loading_) {
        return;
    }
    if (idx < 0 || idx >= (int)items_.size()) {
        return;
    }
    OsdItem& it = items_[idx];
    if (it.kind == VcpKind::Ro || it.kind == VcpKind::Button) {
        return;
    }
    if (value == it.current) {
        return;
    }
    if (record_hist) {
        it.hist.push(it.current);
    }
    it.current = value;
    if (it.kind == VcpKind::Discrete) {
        for (size_t i = 0; i < it.choices.size(); i++) {
            if (it.choices[i] == value) {
                it.choice_idx = (int)i;
                break;
            }
        }
    }
    pending_item_ = idx;
    pending_value_ = value;
    if (debounce) {
        debounce_.Start(100, wxTIMER_ONE_SHOT);
    } else {
        debounce_.Stop();
        flush_pending();
    }
    canvas_->Refresh(false);
}

void OsdFrame::undo_item(int idx) {
    if (idx < 0 || idx >= (int)items_.size()) {
        return;
    }
    uint16_t prev;
    if (!items_[idx].hist.pop(prev)) {
        set_status(_("Nothing to undo"));
        return;
    }
    queue_set_item(idx, prev, false, false);
}

void OsdFrame::reset_item(int idx) {
    if (idx < 0 || idx >= (int)items_.size()) {
        return;
    }
    OsdItem& it = items_[idx];
    if (it.current == it.baseline) {
        set_status(_("Already at default"));
        return;
    }
    queue_set_item(idx, it.baseline, false, true);
    set_status(_("Reset to default"));
}

void OsdFrame::refresh_item_values(const uint8_t* codes, bool all) {
    if (!list_ || list_->size() == 0) {
        return;
    }
    HdmiDisplay& d = list_->at(current_display_);
    if (!d.open()) {
        return;
    }
    for (auto& it : items_) {
        if (it.app_id >= 0 || it.kind == VcpKind::Button) {
            continue;
        }
        bool match = all;
        if (!match && codes) {
            for (const uint8_t* p = codes; *p; p++) {
                if (*p == it.code) {
                    match = true;
                    break;
                }
            }
        }
        if (!match) {
            continue;
        }
        DdcVcpValue v;
        if (d.bus.get_vcp(it.code, v) == DdcErr::Ok && v.valid) {
            it.current = v.current;
            it.max = v.max ? v.max : it.max;
            it.baseline = it.current;
            it.hist.clear();
        }
    }
    canvas_->Refresh(false);
}

void OsdFrame::apply_button(int idx) {
    if (idx < 0 || idx >= (int)items_.size() || !items_[idx].feat) {
        return;
    }
    OsdItem& it = items_[idx];
    HdmiDisplay& d = list_->at(current_display_);
    if (!d.open()) {
        return;
    }
    if (d.bus.set_vcp(it.code, 1) != DdcErr::Ok) {
        set_status(_("Preset failed"));
        return;
    }
    usleep(150000);
    if (it.feat->refresh_all || !it.feat->refresh_codes) {
        refresh_item_values(nullptr, true);
        set_status(_("Factory reset - refreshed all"));
    } else {
        refresh_item_values(it.feat->refresh_codes, false);
        set_status(_("Preset applied - refreshed related"));
    }
}

void OsdFrame::adjust_active(int delta) {
    if (active_item_ < 0 || active_item_ >= (int)items_.size()) {
        return;
    }
    OsdItem& it = items_[active_item_];
    if (it.kind == VcpKind::Continuous) {
        int v = (int)it.current + delta;
        if (v < 0) {
            v = 0;
        }
        if (it.max && v > (int)it.max) {
            v = it.max;
        }
        queue_set_item(active_item_, (uint16_t)v, true, true);
    } else if (it.kind == VcpKind::Discrete && !it.choices.empty()) {
        int ci = it.choice_idx + delta;
        if (ci < 0 || ci >= (int)it.choices.size()) {
            return; /* at end — matches grayed < > */
        }
        queue_set_item(active_item_, it.choices[ci], false, true);
    } else if (it.kind == VcpKind::Button) {
        apply_button(active_item_);
    }
}

bool OsdFrame::activate_shortcut(const std::string& key) {
    for (int i = 0; i < (int)items_.size(); i++) {
        if (!items_[i].feat || !items_[i].feat->shortcut) {
            continue;
        }
        if (key != items_[i].feat->shortcut) {
            continue;
        }
        for (int v = 0; v < (int)visible_cats_.size(); v++) {
            if (visible_cats_[v] == (int)items_[i].feat->cat) {
                cat_index_ = v;
                break;
            }
        }
        active_item_ = i;
        if (items_[i].kind == VcpKind::Button) {
            apply_button(i);
        }
        canvas_->Refresh(false);
        return true;
    }
    return false;
}

/* ---------- canvas ---------- */

wxBEGIN_EVENT_TABLE(OsdCanvas, wxPanel)
    EVT_PAINT(OsdCanvas::on_paint)
    EVT_LEFT_DOWN(OsdCanvas::on_left_down)
    EVT_LEFT_UP(OsdCanvas::on_left_up)
    EVT_MOTION(OsdCanvas::on_motion)
    EVT_MOUSEWHEEL(OsdCanvas::on_mousewheel)
    EVT_CHAR_HOOK(OsdCanvas::on_char_hook)
wxEND_EVENT_TABLE()

OsdCanvas::OsdCanvas(OsdFrame* frame)
    : wxPanel(frame, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS | wxBORDER_NONE),
      frame_(frame) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetFocus();
}

void OsdCanvas::on_paint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    wxSize sz = GetClientSize();
    const OsdPalette& pal = frame_->palette();
    const wxColour icon_tint(255, 255, 255); /* always white glyphs */

    dc.SetBackground(wxBrush(pal.bg));
    dc.Clear();

    draw_round_rect(dc, 1, 1, sz.x - 2, sz.y - 2, 6, pal.bg, &pal.border);

    /* top bar (drag handle for borderless window) */
    draw_round_rect(dc, 2, 2, sz.x - 4, TOP_H, 0, pal.panel);
    dc.SetTextForeground(pal.text);
    dc.SetFont(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    wxString title = tr("No display");
    if (frame_->list() && frame_->list()->size()) {
        title = wxString::FromUTF8(frame_->list()->at(frame_->current_display()).label());
    }
    if (frame_->loading()) {
        title += wxString::FromUTF8(" — ");
        title += wxString::FromUTF8(_("loading"));
    }
    int title_ty = text_vcenter_y(dc, 2, TOP_H);
    dc.DrawText(title, PAD, title_ty);

    auto draw_top_btn = [&](int x, int w, const char* msgid, const char* icon) {
        draw_round_rect(dc, x, 10, w, 28, 4, pal.accent, &pal.border);
        wxBitmap bmp = load_tabler_icon(icon, 16, icon_tint);
        int iy = 10 + (28 - 16) / 2;
        if (bmp.IsOk()) {
            dc.DrawBitmap(bmp, x + 6, iy, true);
        }
        dc.SetTextForeground(pal.text);
        dc.SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        int ty = text_vcenter_y(dc, 10, 28);
        dc.DrawText(tr(msgid), x + 26, ty);
    };
    /* Theme (top-right), then Refresh — single refresh for displays + controls */
    int theme_w = 78;
    int refresh_w = 88;
    int theme_x = sz.x - PAD - theme_w;
    int refresh_x = theme_x - 8 - refresh_w;
    draw_top_btn(refresh_x, refresh_w, _("Refresh"), "refresh");
    draw_top_btn(theme_x, theme_w, _("Theme"), "palette");

    /* sidebar */
    int side_y = TOP_H + PAD;
    int side_h = sz.y - TOP_H - 2 * PAD - 22;
    draw_round_rect(dc, PAD, side_y, SIDEBAR_W, side_h, 4, pal.side);

    dc.SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    const auto& cats = frame_->visible_cats();
    for (int v = 0; v < (int)cats.size(); v++) {
        auto cat = (VcpCategory)cats[v];
        int iy = side_y + 8 + v * 34;
        bool sel = v == frame_->cat_index();
        if (sel) {
            draw_round_rect(dc, PAD + 4, iy, SIDEBAR_W - 8, 30, 3, pal.accent);
        }
        wxBitmap ic = load_tabler_icon(vcp_category_icon(cat), 16, icon_tint);
        int ic_y = iy + (30 - 16) / 2;
        if (ic.IsOk()) {
            dc.DrawBitmap(ic, PAD + 10, ic_y, true);
        }
        dc.SetTextForeground(sel ? pal.active : pal.muted);
        wxString lab = wxString::Format("%d  %s", v + 1, tr(vcp_category_name(cat)));
        dc.DrawText(lab, PAD + 32, text_vcenter_y(dc, iy, 30));
    }

    /* content */
    int cx = PAD + SIDEBAR_W + PAD;
    int cy = side_y;
    int cw = sz.x - cx - PAD;
    int ch = side_h;
    draw_round_rect(dc, cx, cy, cw, ch, 4, pal.bg);

    auto cat = cats.empty() ? VcpCategory::AppOsd : (VcpCategory)cats[frame_->cat_index()];
    int y = cy + 8;
    auto& items = frame_->items();

    for (int i = 0; i < (int)items.size(); i++) {
        OsdItem& it = items[i];
        VcpCategory icat = it.feat ? it.feat->cat : VcpCategory::AppOsd;
        if (icat != cat) {
            continue;
        }
        if (y + ROW_H > cy + ch - 4) {
            break;
        }
        it.row_y = y;
        it.left_x0 = it.left_x1 = it.right_x0 = it.right_x1 = 0;
        it.undo_x0 = it.undo_x1 = it.reset_x0 = it.reset_x1 = 0;
        bool active = i == frame_->active_item();
        if (active) {
            draw_round_rect(dc, cx + 4, y, cw - 8, ROW_H - 2, 3, pal.panel, &pal.active);
        }

        wxString name;
        std::string icon_stem;
        if (it.feat) {
            name = tr(it.feat->name);
            if (it.feat->shortcut) {
                name += " [";
                name += it.feat->shortcut;
                name += "]";
            }
            icon_stem = it.feat->icon ? it.feat->icon : "";
        } else if (!it.choice_labels.empty()) {
            name = tr(it.choice_labels[0]);
            if (it.choice_labels.size() > 1) {
                icon_stem = it.choice_labels[1];
            }
        }

        int row_ty = text_vcenter_y(dc, y, ROW_H);
        int icon_y = y + (ROW_H - 16) / 2;

        if (it.kind == VcpKind::Button) {
            it.btn_x0 = cx + 12;
            it.btn_x1 = cx + cw - 12;
            it.bar_y0 = y + 4;
            it.bar_y1 = y + ROW_H - 8;
            draw_round_rect(dc, (int)it.btn_x0, (int)it.bar_y0, (int)(it.btn_x1 - it.btn_x0),
                            (int)(it.bar_y1 - it.bar_y0), 4,
                            active ? pal.accent : pal.bar_bg, &pal.border);
            if (!icon_stem.empty()) {
                wxBitmap ic = load_tabler_icon(icon_stem, 16, icon_tint);
                if (ic.IsOk()) {
                    dc.DrawBitmap(ic, (int)it.btn_x0 + 10, icon_y, true);
                }
            }
            dc.SetTextForeground(pal.text);
            dc.DrawText(name, (int)it.btn_x0 + 34, row_ty);
            y += ROW_H;
            continue;
        }

        if (!icon_stem.empty()) {
            wxBitmap ic = load_tabler_icon(icon_stem, 16, icon_tint);
            if (ic.IsOk()) {
                dc.DrawBitmap(ic, cx + 10, icon_y, true);
            }
        }
        dc.SetTextForeground(active ? pal.active : pal.text);
        dc.DrawText(name, cx + 32, row_ty);

        int label_w = 170;
        int actions = (it.kind == VcpKind::Ro) ? 0 : (2 * BTN_W + 8);
        it.bar_y0 = y + (ROW_H - 16) / 2;
        it.bar_y1 = it.bar_y0 + 16;

        if (it.kind == VcpKind::Discrete) {
            /* "Color preset: < 7500K >" — arrows grayed when not useful */
            wxString vlab;
            if (it.choice_idx >= 0 && it.choice_idx < (int)it.choice_labels.size()) {
                const std::string& raw = it.choice_labels[it.choice_idx];
                vlab = (raw.size() >= 2 && raw[0] == '0' && raw[1] == 'x') ? wxString::FromUTF8(raw)
                                                                           : tr(raw);
            } else if (it.code == 0x0c && it.app_id < 0) {
                char tmp[40];
                vlab = tr(vcp_color_temp_mode_label(it.current, frame_->color_temp_step(), tmp,
                                                    sizeof(tmp)));
            } else {
                vlab = wxString::Format("%u", (unsigned)it.current);
            }
            wxCoord vw = 0, vh = 0;
            dc.GetTextExtent(vlab, &vw, &vh);
            int value_w = std::max(48, (int)vw + 8);
            int ctrl_w = ARROW_W + 4 + value_w + 4 + ARROW_W;
            int ctrl_x = cx + cw - 12 - actions - ctrl_w;
            if (ctrl_x < cx + 12 + label_w) {
                ctrl_x = cx + 12 + label_w;
            }
            it.left_x0 = ctrl_x;
            it.left_x1 = ctrl_x + ARROW_W;
            it.bar_x0 = it.left_x1 + 4;
            it.bar_x1 = it.bar_x0 + value_w;
            it.right_x0 = it.bar_x1 + 4;
            it.right_x1 = it.right_x0 + ARROW_W;

            bool can_prev = it.choice_idx > 0;
            bool can_next = it.choice_idx >= 0 && it.choice_idx + 1 < (int)it.choices.size();
            dc.SetFont(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
            dc.SetTextForeground(can_prev ? pal.text : pal.muted);
            dc.DrawText("<", (int)it.left_x0 + 4, row_ty);
            dc.SetTextForeground(pal.text);
            dc.SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
            dc.DrawText(vlab, (int)it.bar_x0 + 4, row_ty);
            dc.SetFont(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
            dc.SetTextForeground(can_next ? pal.text : pal.muted);
            dc.DrawText(">", (int)it.right_x0 + 4, row_ty);
            dc.SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        } else {
            it.bar_x0 = cx + 12 + label_w;
            it.bar_x1 = cx + cw - 12 - actions - 64;
            if (it.bar_x1 < it.bar_x0 + 40) {
                it.bar_x1 = it.bar_x0 + 40;
            }
            double maxv = it.max ? it.max : 100;
            double frac = maxv > 0 ? it.current / maxv : 0;
            frac = std::clamp(frac, 0.0, 1.0);
            draw_round_rect(dc, (int)it.bar_x0, (int)it.bar_y0, (int)(it.bar_x1 - it.bar_x0), 16, 3,
                            pal.bar_bg);
            draw_round_rect(dc, (int)it.bar_x0, (int)it.bar_y0,
                            (int)((it.bar_x1 - it.bar_x0) * frac), 16, 3,
                            active ? pal.active : pal.bar);

            char vbuf[40];
            snprintf(vbuf, sizeof(vbuf), "%u", (unsigned)it.current);
            dc.SetTextForeground(pal.text);
            dc.DrawText(vbuf, (int)it.bar_x1 + 8, row_ty);
        }

        if (it.kind != VcpKind::Ro) {
            double ux = cx + cw - 12 - 2 * BTN_W - 4;
            double rx = cx + cw - 12 - BTN_W;
            it.undo_x0 = ux;
            it.undo_x1 = ux + BTN_W;
            it.reset_x0 = rx;
            it.reset_x1 = rx + BTN_W;
            wxBitmap u = load_tabler_icon("arrow-back-up", 16, icon_tint);
            wxBitmap r = load_tabler_icon("restore", 16, icon_tint);
            if (u.IsOk()) {
                dc.DrawBitmap(u, (int)ux + 6, icon_y, true);
            }
            if (r.IsOk()) {
                dc.DrawBitmap(r, (int)rx + 6, icon_y, true);
            }
        }
        y += ROW_H;
    }

    /* status */
    dc.SetTextForeground(pal.muted);
    dc.SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    dc.DrawText(frame_->status(), PAD, sz.y - 18);
}

int OsdCanvas::hit_top_btn(int x, int y) const {
    wxSize sz = GetClientSize();
    int theme_w = 78;
    int refresh_w = 88;
    int theme_x = sz.x - PAD - theme_w;
    int refresh_x = theme_x - 8 - refresh_w;
    if (y >= 10 && y <= 38) {
        if (x >= refresh_x && x <= refresh_x + refresh_w) {
            return 0; /* refresh */
        }
        if (x >= theme_x && x <= theme_x + theme_w) {
            return 1; /* theme */
        }
    }
    return -1;
}

int OsdCanvas::hit_sidebar(int x, int y) const {
    int side_y = TOP_H + PAD;
    if (x < PAD || x > PAD + SIDEBAR_W) {
        return -1;
    }
    for (int v = 0; v < (int)frame_->visible_cats().size(); v++) {
        int iy = side_y + 8 + v * 34;
        if (y >= iy && y <= iy + 30) {
            return v;
        }
    }
    return -1;
}

int OsdCanvas::hit_item(int x, int y, int* zone) const {
    *zone = 0;
    auto cat = frame_->visible_cats().empty()
                   ? VcpCategory::AppOsd
                   : (VcpCategory)frame_->visible_cats()[frame_->cat_index()];
    auto& items = frame_->items();
    for (int i = 0; i < (int)items.size(); i++) {
        const OsdItem& it = items[i];
        VcpCategory icat = it.feat ? it.feat->cat : VcpCategory::AppOsd;
        if (icat != cat) {
            continue;
        }
        if (y < it.row_y || y > it.row_y + ROW_H) {
            continue;
        }
        if (it.kind == VcpKind::Button) {
            *zone = 3;
            return i;
        }
        if (it.undo_x1 > it.undo_x0 && x >= it.undo_x0 && x <= it.undo_x1) {
            *zone = 1;
            return i;
        }
        if (it.reset_x1 > it.reset_x0 && x >= it.reset_x0 && x <= it.reset_x1) {
            *zone = 2;
            return i;
        }
        if (it.kind == VcpKind::Discrete) {
            if (it.left_x1 > it.left_x0 && x >= it.left_x0 && x <= it.left_x1) {
                *zone = 4;
                return i;
            }
            if (it.right_x1 > it.right_x0 && x >= it.right_x0 && x <= it.right_x1) {
                *zone = 5;
                return i;
            }
        }
        return i;
    }
    return -1;
}

void OsdCanvas::on_left_down(wxMouseEvent& ev) {
    int top = hit_top_btn(ev.GetX(), ev.GetY());
    if (top == 0) {
        frame_->refresh_all();
        return;
    }
    if (top == 1) {
        frame_->popup_theme_menu();
        return;
    }
    if (ev.GetY() < TOP_H) {
        /* Borderless: drag window from the title/top bar */
        win_drag_ = true;
        win_drag_origin_ = frame_->GetPosition();
        win_drag_start_ = ev.GetPosition();
        ClientToScreen(&win_drag_start_.x, &win_drag_start_.y);
        CaptureMouse();
        return;
    }
    int side = hit_sidebar(ev.GetX(), ev.GetY());
    if (side >= 0) {
        frame_->set_cat_index(side);
        return;
    }
    int zone = 0;
    int idx = hit_item(ev.GetX(), ev.GetY(), &zone);
    if (idx < 0) {
        return;
    }
    frame_->set_active_item(idx);
    OsdItem& it = frame_->items()[idx];
    if (zone == 1) {
        frame_->undo_item(idx);
        return;
    }
    if (zone == 2) {
        frame_->reset_item(idx);
        return;
    }
    if (zone == 4) {
        frame_->adjust_active(-1);
        return;
    }
    if (zone == 5) {
        frame_->adjust_active(1);
        return;
    }
    if (it.kind == VcpKind::Button) {
        frame_->apply_button(idx);
        return;
    }
    if (it.kind == VcpKind::Continuous && ev.GetX() >= it.bar_x0 && ev.GetX() <= it.bar_x1) {
        double frac = (ev.GetX() - it.bar_x0) / (it.bar_x1 - it.bar_x0);
        uint16_t maxv = it.max ? it.max : 100;
        uint16_t v = (uint16_t)(frac * maxv + 0.5);
        it.hist.push(it.current);
        drag_item_ = idx;
        drag_hist_pushed_ = true;
        frame_->queue_set_item(idx, v, true, false);
        return;
    }
    Refresh(false);
}

void OsdCanvas::on_left_up(wxMouseEvent&) {
    if (win_drag_) {
        win_drag_ = false;
        if (HasCapture()) {
            ReleaseMouse();
        }
    }
    drag_item_ = -1;
    drag_hist_pushed_ = false;
}

void OsdCanvas::on_motion(wxMouseEvent& ev) {
    if (win_drag_ && ev.Dragging() && ev.LeftIsDown()) {
        wxPoint cur = ev.GetPosition();
        ClientToScreen(&cur.x, &cur.y);
        frame_->Move(win_drag_origin_ + (cur - win_drag_start_));
        return;
    }
    if (drag_item_ < 0 || !ev.Dragging() || !ev.LeftIsDown()) {
        return;
    }
    OsdItem& it = frame_->items()[drag_item_];
    if (it.kind != VcpKind::Continuous) {
        return;
    }
    double frac = (ev.GetX() - it.bar_x0) / (it.bar_x1 - it.bar_x0);
    frac = std::clamp(frac, 0.0, 1.0);
    uint16_t maxv = it.max ? it.max : 100;
    uint16_t v = (uint16_t)(frac * maxv + 0.5);
    frame_->queue_set_item(drag_item_, v, true, false);
}

void OsdCanvas::on_mousewheel(wxMouseEvent& ev) {
    int zone = 0;
    int idx = hit_item(ev.GetX(), ev.GetY(), &zone);
    if (idx >= 0) {
        frame_->set_active_item(idx);
    }
    frame_->adjust_active(ev.GetWheelRotation() > 0 ? 1 : -1);
}

void OsdCanvas::on_char_hook(wxKeyEvent& ev) {
    int key = ev.GetKeyCode();
    if (key == WXK_ESCAPE || key == 'Q' || key == 'q') {
        frame_->Close();
        return;
    }
    if (key == WXK_F5) {
        frame_->refresh_all();
        return;
    }
    if (key == WXK_TAB) {
        auto cat = frame_->visible_cats().empty()
                       ? VcpCategory::AppOsd
                       : (VcpCategory)frame_->visible_cats()[frame_->cat_index()];
        int dir = ev.ShiftDown() ? -1 : 1;
        int start = frame_->active_item();
        int n = (int)frame_->items().size();
        for (int step = 1; step <= n; step++) {
            int i = (start + dir * step + n * 8) % n;
            VcpCategory c =
                frame_->items()[i].feat ? frame_->items()[i].feat->cat : VcpCategory::AppOsd;
            if (c == cat && frame_->items()[i].kind != VcpKind::Ro) {
                frame_->set_active_item(i);
                Refresh(false);
                return;
            }
        }
        return;
    }
    if (key == WXK_LEFT || key == WXK_DOWN || key == '-') {
        frame_->adjust_active(-1);
        return;
    }
    if (key == WXK_RIGHT || key == WXK_UP || key == '+' || key == '=') {
        frame_->adjust_active(1);
        return;
    }
    if (key == WXK_PAGEUP) {
        frame_->adjust_active(5);
        return;
    }
    if (key == WXK_PAGEDOWN) {
        frame_->adjust_active(-5);
        return;
    }
    if (ev.ControlDown() && (key == 'Z' || key == 'z')) {
        frame_->undo_item(frame_->active_item());
        return;
    }
    if (ev.ControlDown() && (key == 'R' || key == 'r')) {
        frame_->reset_item(frame_->active_item());
        return;
    }
    if (key == '[') {
        int n = (int)frame_->list()->size();
        if (n) {
            frame_->set_current_display((frame_->current_display() - 1 + n) % n);
        }
        return;
    }
    if (key == ']') {
        int n = (int)frame_->list()->size();
        if (n) {
            frame_->set_current_display((frame_->current_display() + 1) % n);
        }
        return;
    }
    if (key >= '1' && key <= '9') {
        frame_->set_cat_index(key - '1');
        return;
    }
    if (key >= 'a' && key <= 'z') {
        char s[2] = {(char)key, 0};
        if (frame_->activate_shortcut(s)) {
            return;
        }
    }
    if (key >= 'A' && key <= 'Z') {
        char s[2] = {(char)(key - 'A' + 'a'), 0};
        if (frame_->activate_shortcut(s)) {
            return;
        }
    }
    if (key == '0') {
        frame_->activate_shortcut("0");
        return;
    }
    ev.Skip();
}

void OsdCanvas::on_key_down(wxKeyEvent& ev) { on_char_hook(ev); }
