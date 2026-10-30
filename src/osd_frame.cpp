/*
 * Copyright (C) 2026 Lenik <hdmiosd@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "osd_frame.hpp"

#include "osd_draw.hpp"

#include "config.h"
#include "icons.hpp"

#include <bas/locale/i18n.h>
#include <bas/log/uselog.h>

#include <wx/cursor.h>
#include <wx/dc.h>
#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/notifmsg.h>
#include <wx/popupwin.h>
#include <wx/sizer.h>
#include <wx/uiaction.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unistd.h>

wxBEGIN_EVENT_TABLE(OsdFrame, wxFrame)
    EVT_TIMER(wxID_ANY, OsdFrame::on_debounce)
wxEND_EVENT_TABLE()

OsdFrame::OsdFrame(DisplayList* list, Settings* settings, const std::string& device_filter)
    : wxFrame(nullptr, wxID_ANY, "hdmiosd", wxDefaultPosition, wxSize(640, 480),
              /* Avoid wxFRAME_TOOL_WINDOW — many WMs never map it as a visible OSD. */
              wxBORDER_NONE | wxSTAY_ON_TOP),
      list_(list), settings_(settings), device_filter_(device_filter),
      alive_(std::make_shared<std::atomic<bool>>(true)) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    /* Keep the WM from collapsing an unrealized borderless frame to 1×1. */
    SetMinClientSize(wxSize(320, 240));
    palette_ = theme_default_dark();
    if (settings_ && !settings_->app.theme.empty()) {
        theme_load(settings_->app.theme, palette_);
    }
    wxBitmap app_ic = load_app_icon(64);
    if (app_ic.IsOk()) {
        wxIcon icon;
        icon.CopyFromBitmap(app_ic);
        SetIcon(icon);
    } else {
        logwarn("app icon not loaded from assets");
    }
    canvas_ = new OsdCanvas(this);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(canvas_, 1, wxEXPAND);
    SetSizer(sizer);
    debounce_.SetOwner(this);
    int cw = settings_ && settings_->app.width > 0 ? settings_->app.width : 640;
    int ch = settings_ && settings_->app.height > 0 ? settings_->app.height : 480;
    SetClientSize(cw, ch);
    /* Shell UI only here — Show + place + scan happen in present() after map. */
    show_shell_ui(_("Scanning displays…"));
    canvas_->SetFocus();
}

void OsdFrame::present() {
    /* wxGTK: SetTransparent must run before Show. It can recreate the X
       window as 1×1, so re-apply size/position immediately after mapping. */
    auto& a = settings_->app;
    int cw = a.width > 0 ? a.width : 640;
    int ch = a.height > 0 ? a.height : 480;
    SetMinClientSize(wxSize(320, 240));
    SetClientSize(cw, ch);
    SetTransparent((wxByte)std::clamp((int)(a.opacity * 255), 30, 255));
    Show(true);
    apply_window_placement();
    Raise();
    SetFocus();
    if (canvas_) {
        canvas_->SetFocus();
        canvas_->Refresh(false);
        canvas_->Update();
    }
    /* Discover only after the shell UI is mapped — never block Show. */
    CallAfter([this]() {
        if (!alive_ || !alive_->load()) {
            return;
        }
        start_async_scan();
    });
}

OsdFrame::~OsdFrame() {
    ++load_gen_;
    if (alive_) {
        alive_->store(false);
    }
    /* Detach only — joining here can hang if an I2C probe is stuck. */
    if (worker_.joinable()) {
        worker_.detach();
    }
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
        VcpCategory c = items_[j].feat ? items_[j].feat->cat : VcpCategory::SoftOsd;
        if (c == cat && items_[j].kind != VcpKind::Ro) {
            active_item_ = j;
            break;
        }
    }
    if (canvas_) {
        canvas_->Refresh(false);
    }
}









void OsdFrame::save_settings() {
    if (settings_) {
        settings_->save();
    }
}

void OsdFrame::build_app_osd_items() {
    auto add = [&](int id, const char* name, const char* icon, const char* desc, uint16_t cur,
                   uint16_t max) {
        OsdItem it;
        it.app_id = id;
        it.kind = VcpKind::Continuous;
        it.current = cur;
        it.max = max;
        it.baseline = cur;
        it.code = (uint8_t)id;
        it.choice_labels.push_back(name); /* English msgid */
        it.choice_labels.push_back(icon ? icon : "apps");
        if (desc) {
            it.description = desc;
        }
        items_.push_back(std::move(it));
    };
    auto& a = settings_->app;
    add(APP_OPACITY, N_("Opacity %"), "apps",
        N_("Transparency of this Soft OSD window over the desktop."),
        (uint16_t)(a.opacity * 100 + 0.5), 100);
    add(APP_WIDTH, N_("Width"), "layout-sidebar", N_("Soft OSD window width in pixels."),
        (uint16_t)std::clamp(a.width, 200, 4000), 2000);
    add(APP_HEIGHT, N_("Height"), "layout-sidebar", N_("Soft OSD window height in pixels."),
        (uint16_t)std::clamp(a.height, 160, 3000), 1600);
    add(APP_LEFT, N_("Margin left"), "arrow-back",
        N_("Distance from the left edge of the current monitor."), (uint16_t)a.margin_left, 400);
    add(APP_RIGHT, N_("Margin right"), "arrow-forward",
        N_("Distance from the right edge of the current monitor."), (uint16_t)a.margin_right, 400);
    add(APP_TOP, N_("Margin top"), "arrow-back-up",
        N_("Distance from the top edge of the current monitor."), (uint16_t)a.margin_top, 400);
    add(APP_BOTTOM, N_("Margin bottom"), "arrow-back",
        N_("Distance from the bottom edge of the current monitor."), (uint16_t)a.margin_bottom, 400);
}

void OsdFrame::show_shell_ui(const std::string& status_msg) {
    items_.clear();
    visible_cats_.clear();
    active_item_ = -1;
    color_temp_step_ = 100;
    visible_cats_.push_back((int)VcpCategory::SoftOsd);
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
            cat_has[(int)VcpCategory::SoftOsd] = true;
        } else if (it.feat) {
            cat_has[(int)it.feat->cat] = true;
        }
    }
    cat_has[(int)VcpCategory::SoftOsd] = true; /* always listed */
    int prev = cat_index_ < (int)visible_cats_.size() ? visible_cats_[cat_index_] : -1;
    visible_cats_.clear();
    /* Enum order == sidebar order (Display … System). */
    for (int c = 0; c < (int)VcpCategory::Count; c++) {
        if (cat_has[c]) {
            visible_cats_.push_back(c);
        }
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
    /* Never join on the UI thread — a stuck I2C probe would freeze the window.
       Cancelled workers exit via load_gen_/alive_ checks and self-detach. */
    if (worker_.joinable()) {
        worker_.detach();
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
    if (feat.description) {
        it.description = feat.description;
    }
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
    loginfo("async scan: starting background discovery");
    worker_ = std::thread([this, alive, gen, filter]() {
        auto cands = DisplayList::enumerate_candidates(filter);
        size_t found = 0;
        for (size_t i = 0; i < cands.size(); i++) {
            if (!alive->load() || gen != load_gen_.load()) {
                loginfo("async scan: cancelled");
                return;
            }
            loginfo_fmt("async scan: probe %zu/%zu", i + 1, cands.size());
            HdmiDisplay d;
            if (!DisplayList::probe_candidate(cands[i], d)) {
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
        loginfo_fmt("async scan: done, found=%zu", found);
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
    loginfo_fmt("async features: reading VCP from %s", path.c_str());

    if (worker_.joinable()) {
        worker_.detach();
    }
    worker_ = std::thread([this, alive, gen, path, caps, disp]() mutable {
        HdmiDisplay probe;
        probe.i2c_path = path;
        probe.caps = caps;
        if (probe.bus.open(path) != DdcErr::Ok) {
            logwarn_fmt("async features: cannot open %s", path.c_str());
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

        int added = 0;
        for (const auto& feat : vcp_features()) {
            if (!alive->load() || gen != load_gen_.load()) {
                probe.bus.close();
                loginfo("async features: cancelled");
                return;
            }
            logdebug_fmt("async features: try 0x%02X %s", feat.code, feat.name);
            OsdItem it;
            if (!make_item_from_feat(probe, feat, it)) {
                continue;
            }
            ++added;
            loginfo_fmt("async features: + 0x%02X %s = %u", feat.code, feat.name,
                        (unsigned)it.current);
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
        loginfo_fmt("async features: done, %d controls", added);
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

void OsdFrame::cancel_pending_sets() {
    debounce_.Stop();
    pending_item_ = -1;
}

void OsdFrame::set_window_dragging(bool on) {
    window_dragging_ = on;
    if (on) {
        /* Drop any deferred VCP/App set so a late flush cannot Move/pin mid-drag. */
        cancel_pending_sets();
    }
}


void OsdFrame::flush_pending() {
    if (pending_item_ < 0 || pending_item_ >= (int)items_.size()) {
        return;
    }
    OsdItem& it = items_[pending_item_];
    if (it.app_id >= 0) {
        auto& a = settings_->app;
        const bool margin = is_margin_app(it.app_id);
        /* Moving the OSD updates margins via sync_margins_from_position — never
           re-enter placement/pin from a margin onchange while dragging. */
        if (margin && window_dragging_) {
            pending_item_ = -1;
            return;
        }
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
            break;
        case APP_RIGHT:
            a.margin_right = pending_value_;
            a.have_right = true;
            a.have_left = false;
            break;
        case APP_TOP:
            a.margin_top = pending_value_;
            a.have_top = true;
            a.have_bottom = false;
            break;
        case APP_BOTTOM:
            a.margin_bottom = pending_value_;
            a.have_bottom = true;
            a.have_top = false;
            break;
        }
        it.current = pending_value_;
        settings_->save();
        set_status(_("App OSD updated"));
        if (margin && canvas_) {
            /* handlers off → move → pin thumb → rebaseline → on */
            canvas_->set_mouse_events_enabled(false);
            apply_window_placement();
            wxPoint origin = last_place_origin_;
            canvas_->pin_cursor_to_slider(pending_item_, pending_value_, &origin);
            canvas_->rebaseline_margin_drag(pending_value_);
            canvas_->set_mouse_events_enabled(true);
        }
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
    /* Window drag owns margin geometry — ignore slider/onchange queues. */
    if (window_dragging_ && is_margin_app(it.app_id)) {
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
    /* Margins reposition the window — apply immediately so the pointer can be
       pinned to the exact slider thumb for this value. */
    if (debounce && !is_margin_app(it.app_id)) {
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

