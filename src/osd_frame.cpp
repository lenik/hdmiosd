/*
 * Copyright (C) 2026 Lenik <hdmiosd@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "osd_frame.hpp"

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

namespace {
constexpr int SIDEBAR_W = 168;
constexpr int ROW_H = 36;
constexpr int TOP_H = 48;
constexpr int BTN_W = 28;
constexpr int PAD = 10;       /* outer inset from window edge */
constexpr int INNER_PAD = 10; /* padding inside sidebar / content panes */
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

enum class ThemeRowKind { Header, Item, Sep };

struct ThemeRow {
    ThemeRowKind kind = ThemeRowKind::Item;
    wxString label;
    std::string path;
    int y = 0;
    int h = 0;
};

class ThemePickerPopup : public wxPopupTransientWindow {
  public:
    ThemePickerPopup(OsdFrame* owner, std::vector<ThemeRow> rows, const OsdPalette& pal)
        : wxPopupTransientWindow(owner, wxBORDER_NONE), owner_(owner), rows_(std::move(rows)),
          pal_(pal) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &ThemePickerPopup::on_paint, this);
        Bind(wxEVT_LEFT_DOWN, &ThemePickerPopup::on_left, this);
        Bind(wxEVT_MOTION, &ThemePickerPopup::on_motion, this);
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
            hover_ = -1;
            Refresh(false);
        });
        layout();
    }

    void layout() {
        wxMemoryDC probe;
        probe.SetFont(osd_typeface(OsdTypeRole::Menu));
        int max_w = 160;
        int y = 6;
        for (auto& r : rows_) {
            if (r.kind == ThemeRowKind::Sep) {
                r.y = y;
                r.h = 7;
                y += r.h;
                continue;
            }
            wxFont f = r.kind == ThemeRowKind::Header ? osd_typeface(OsdTypeRole::MenuHeader)
                                                      : osd_typeface(OsdTypeRole::Menu);
            probe.SetFont(f);
            wxCoord tw = 0, th = 0;
            probe.GetTextExtent(r.label, &tw, &th);
            max_w = std::max(max_w, (int)tw + 28);
            r.h = r.kind == ThemeRowKind::Header ? 22 : 26;
            r.y = y;
            y += r.h;
        }
        SetClientSize(max_w, y + 6);
    }

  private:
    OsdFrame* owner_;
    std::vector<ThemeRow> rows_;
    OsdPalette pal_;
    int hover_ = -1;

    int hit(int y) const {
        for (int i = 0; i < (int)rows_.size(); i++) {
            if (rows_[i].kind != ThemeRowKind::Item) {
                continue;
            }
            if (y >= rows_[i].y && y < rows_[i].y + rows_[i].h) {
                return i;
            }
        }
        return -1;
    }

    void on_paint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        wxSize sz = GetClientSize();
        dc.SetBackground(wxBrush(pal_.menu_bg));
        dc.Clear();
        draw_round_rect(dc, 0, 0, sz.x, sz.y, 4, pal_.menu_bg, &pal_.border);
        for (int i = 0; i < (int)rows_.size(); i++) {
            const auto& r = rows_[i];
            if (r.kind == ThemeRowKind::Sep) {
                dc.SetPen(wxPen(pal_.menu_sep, 1));
                int yy = r.y + r.h / 2;
                dc.DrawLine(10, yy, sz.x - 10, yy);
                continue;
            }
            bool sel = (i == hover_);
            if (sel && r.kind == ThemeRowKind::Item) {
                draw_round_rect(dc, 4, r.y, sz.x - 8, r.h, 3, pal_.menu_sel_bg);
            }
            if (r.kind == ThemeRowKind::Header) {
                dc.SetFont(osd_typeface(OsdTypeRole::MenuHeader));
                dc.SetTextForeground(pal_.menu_header_fg);
            } else {
                dc.SetFont(osd_typeface(OsdTypeRole::Menu));
                dc.SetTextForeground(sel ? pal_.menu_sel_fg : pal_.menu_fg);
            }
            dc.DrawText(r.label, 12, text_vcenter_y(dc, r.y, r.h));
        }
    }

    void on_motion(wxMouseEvent& ev) {
        int h = hit(ev.GetY());
        if (h != hover_) {
            hover_ = h;
            Refresh(false);
        }
    }

    void on_left(wxMouseEvent& ev) {
        int h = hit(ev.GetY());
        if (h >= 0 && !rows_[h].path.empty()) {
            std::string path = rows_[h].path;
            Dismiss();
            owner_->apply_theme(path);
            return;
        }
        Dismiss();
    }
};
} // namespace

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

wxRect OsdFrame::monitor_client_at(wxPoint pt) const {
    int disp_idx = wxDisplay::GetFromPoint(pt);
    if (disp_idx == wxNOT_FOUND) {
        disp_idx = wxDisplay::GetFromWindow(const_cast<OsdFrame*>(this));
    }
    if (disp_idx == wxNOT_FOUND) {
        disp_idx = 0;
    }
    wxDisplay disp((unsigned)disp_idx);
    wxRect geo = disp.GetClientArea();
    if (geo.width <= 0 || geo.height <= 0) {
        geo = disp.GetGeometry();
    }
    return geo;
}

wxPoint OsdFrame::placement_origin() const {
    auto& a = settings_->app;
    int cw = a.width > 0 ? a.width : 640;
    int ch = a.height > 0 ? a.height : 480;
    int w = 0, h = 0;
    OsdFrame* self = const_cast<OsdFrame*>(this);
    self->GetSize(&w, &h);
    if (w < 320 || h < 240) {
        w = cw;
        h = ch;
    }

    /* Always the monitor under the pointer — never the virtual-desktop union. */
    wxPoint mouse = wxGetMousePosition();
    wxRect geo = monitor_client_at(mouse);
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
    if (x < geo.x) {
        x = geo.x;
    }
    if (y < geo.y) {
        y = geo.y;
    }
    if (x + w > geo.x + geo.width) {
        x = geo.x + std::max(0, geo.width - w);
    }
    if (y + h > geo.y + geo.height) {
        y = geo.y + std::max(0, geo.height - h);
    }
    return wxPoint(x, y);
}

void OsdFrame::apply_window_placement() {
    auto& a = settings_->app;
    int cw = a.width > 0 ? a.width : 640;
    int ch = a.height > 0 ? a.height : 480;
    /* Set size before and after Show — wxGTK often ignores pre-map SetClientSize
       on borderless frames (ends up as a 1×1 invisible window). */
    SetMinClientSize(wxSize(320, 240));
    SetClientSize(cw, ch);
    SetSize(GetSize());
    Layout();

    wxPoint mouse = wxGetMousePosition();
    wxRect geo = monitor_client_at(mouse);
    int disp_idx = wxDisplay::GetFromPoint(mouse);
    if (disp_idx == wxNOT_FOUND) {
        disp_idx = wxDisplay::GetFromWindow(this);
    }
    if (disp_idx == wxNOT_FOUND) {
        disp_idx = 0;
    }
    int w = cw, h = ch;
    GetSize(&w, &h);
    if (w < 320 || h < 240) {
        w = cw;
        h = ch;
        SetSize(w, h);
    }
    wxPoint origin = placement_origin();
    int x = origin.x, y = origin.y;
    SetSize(x, y, w, h);
    last_place_origin_ = origin;
    loginfo_fmt("OSD placement: display=%d mouse=(%d,%d) geo=%dx%d+%d+%d "
                "window=%dx%d+%d+%d opacity=%.2f shown=%d",
                disp_idx, mouse.x, mouse.y, geo.width, geo.height, geo.x, geo.y, w, h, x, y,
                a.opacity, IsShown() ? 1 : 0);
}

int OsdFrame::find_app_item(int app_id) const {
    for (int i = 0; i < (int)items_.size(); i++) {
        if (items_[(size_t)i].app_id == app_id) {
            return i;
        }
    }
    return -1;
}

void OsdFrame::sync_margins_from_position(wxPoint frame_pos, bool save) {
    if (!settings_) {
        return;
    }
    int w = 0, h = 0;
    GetSize(&w, &h);
    if (w < 1 || h < 1) {
        return;
    }
    /* Current monitor under the pointer — never the virtual-desktop union. */
    wxRect geo = monitor_client_at(wxGetMousePosition());

    int left = std::max(0, frame_pos.x - geo.x);
    int right = std::max(0, (geo.x + geo.width) - (frame_pos.x + w));
    int top = std::max(0, frame_pos.y - geo.y);
    int bottom = std::max(0, (geo.y + geo.height) - (frame_pos.y + h));

    auto& a = settings_->app;
    constexpr int kMaxMargin = 400;
    auto set_item = [&](int app_id, int value) {
        int idx = find_app_item(app_id);
        if (idx >= 0) {
            items_[(size_t)idx].current = (uint16_t)std::clamp(value, 0, kMaxMargin);
        }
    };

    int prev_h = a.have_left ? APP_LEFT : (a.have_right ? APP_RIGHT : -1);
    int prev_v = a.have_top ? APP_TOP : (a.have_bottom ? APP_BOTTOM : -1);
    int prev_h_val = a.have_left ? a.margin_left : (a.have_right ? a.margin_right : 0);
    int prev_v_val = a.have_top ? a.margin_top : (a.have_bottom ? a.margin_bottom : 0);

    int h_app;
    int h_val;
    if (left <= right) {
        h_app = APP_LEFT;
        h_val = std::clamp(left, 0, kMaxMargin);
        a.margin_left = h_val;
        a.have_left = true;
        a.have_right = false;
    } else {
        h_app = APP_RIGHT;
        h_val = std::clamp(right, 0, kMaxMargin);
        a.margin_right = h_val;
        a.have_right = true;
        a.have_left = false;
    }
    set_item(h_app, h_val);

    int v_app;
    int v_val;
    if (top <= bottom) {
        v_app = APP_TOP;
        v_val = std::clamp(top, 0, kMaxMargin);
        a.margin_top = v_val;
        a.have_top = true;
        a.have_bottom = false;
    } else {
        v_app = APP_BOTTOM;
        v_val = std::clamp(bottom, 0, kMaxMargin);
        a.margin_bottom = v_val;
        a.have_bottom = true;
        a.have_top = false;
    }
    set_item(v_app, v_val);

    int h_delta = (h_app != prev_h ? 1000 : 0) + std::abs(h_val - prev_h_val);
    int v_delta = (v_app != prev_v ? 1000 : 0) + std::abs(v_val - prev_v_val);
    int highlight = -1;
    if (h_delta > 0 || v_delta > 0) {
        highlight = (v_delta >= h_delta) ? v_app : h_app;
    }
    if (highlight >= 0) {
        for (int i = 0; i < (int)visible_cats_.size(); i++) {
            if (visible_cats_[(size_t)i] == (int)VcpCategory::AppOsd) {
                cat_index_ = i;
                break;
            }
        }
        int idx = find_app_item(highlight);
        if (idx >= 0) {
            active_item_ = idx;
        }
    }

    if (save) {
        settings_->save();
    }
    if (canvas_) {
        canvas_->Refresh(false);
    }
}

void OsdFrame::apply_theme(const std::string& path) {
    OsdPalette pal;
    std::string name;
    if (!theme_load(path, pal, &name)) {
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
    if (!name.empty()) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s: %s", _("Theme"), name.c_str());
        set_status(buf);
    } else {
        set_status(_("Theme applied"));
    }
}

void OsdFrame::cycle_theme(int dir) {
    auto themes = theme_scan();
    if (themes.empty()) {
        set_status(_("No minor-themes found (set MINOR_THEMES_DIR)"));
        return;
    }
    const int n = (int)themes.size();
    int cur = 0;
    if (settings_ && !settings_->app.theme.empty()) {
        for (int i = 0; i < n; i++) {
            if (themes[(size_t)i].path == settings_->app.theme) {
                cur = i;
                break;
            }
        }
    }
    int next = (cur + dir + n * 64) % n;
    const ThemeEntry& t = themes[(size_t)next];
    apply_theme(t.path);
    wxString title = wxString::FromUTF8(_("Theme"));
    wxString body = wxString::FromUTF8(t.name.empty() ? t.path.c_str() : t.name.c_str());
    wxNotificationMessage note(title, body, this);
    note.Show(3);
}

void OsdFrame::popup_theme_menu() {
    auto themes = theme_scan();
    if (themes.empty()) {
        set_status(_("No minor-themes found (set MINOR_THEMES_DIR)"));
        return;
    }
    std::vector<ThemeRow> rows;
    auto push_group = [&](const char* title, const std::string& group) {
        bool any = false;
        for (const auto& t : themes) {
            if (t.group == group) {
                any = true;
                break;
            }
        }
        if (!any) {
            return;
        }
        if (!rows.empty()) {
            rows.push_back({ThemeRowKind::Sep, {}, {}});
        }
        rows.push_back({ThemeRowKind::Header, wxString::FromUTF8(title), {}});
        auto push_type = [&](const char* type_label, const char* type) {
            bool have = false;
            for (const auto& t : themes) {
                if (t.group == group && t.type == type) {
                    have = true;
                    break;
                }
            }
            if (!have) {
                return;
            }
            rows.push_back({ThemeRowKind::Header, wxString::FromUTF8(type_label), {}});
            for (const auto& t : themes) {
                if (t.group != group || t.type != type) {
                    continue;
                }
                rows.push_back({ThemeRowKind::Item, wxString::FromUTF8(t.name.c_str()), t.path});
            }
        };
        push_type(_("— Light —"), "light");
        push_type(_("— Dark —"), "dark");
    };
    push_group(_("Minor"), "minor");
    push_group(_("Vibe"), "vibe");
    push_group(_("Country"), "country");

    auto* pop = new ThemePickerPopup(this, std::move(rows), palette_);
    wxPoint pt = ClientToScreen(wxPoint(0, 0));
    if (canvas_) {
        pt = canvas_->ClientToScreen(wxPoint(canvas_->theme_btn_x(), TOP_H));
    }
    pop->Position(pt, wxSize(0, 0));
    pop->Popup();
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

static bool is_margin_app(int app_id) {
    return app_id == APP_LEFT || app_id == APP_RIGHT || app_id == APP_TOP || app_id == APP_BOTTOM;
}

void OsdFrame::flush_pending() {
    if (pending_item_ < 0 || pending_item_ >= (int)items_.size()) {
        return;
    }
    OsdItem& it = items_[pending_item_];
    if (it.app_id >= 0) {
        auto& a = settings_->app;
        const bool margin = is_margin_app(it.app_id);
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
    const wxColour icon_tint = pal.icon;

    dc.SetBackground(wxBrush(pal.editor_bg));
    dc.Clear();

    draw_round_rect(dc, 1, 1, sz.x - 2, sz.y - 2, 6, pal.editor_bg, &pal.border);

    /* titleBar */
    draw_round_rect(dc, 2, 2, sz.x - 4, TOP_H, 0, pal.panel_bg);
    dc.SetTextForeground(pal.title_fg);
    dc.SetFont(osd_typeface(OsdTypeRole::Title));
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

    auto measure_top_btn = [&](const wxString& label) {
        dc.SetFont(osd_typeface(OsdTypeRole::Button));
        wxCoord tw = 0, th = 0;
        dc.GetTextExtent(label, &tw, &th);
        return std::max(72, (int)tw + 36); /* icon + padding */
    };
    auto draw_top_btn = [&](int x, int w, const wxString& label, const char* icon) {
        draw_round_rect(dc, x, 10, w, 28, 4, pal.button_bg, &pal.button_border);
        wxBitmap bmp = load_tabler_icon(icon, 16, pal.button_fg);
        int iy = 10 + (28 - 16) / 2;
        if (bmp.IsOk()) {
            dc.DrawBitmap(bmp, x + 6, iy, true);
        }
        dc.SetTextForeground(pal.button_fg);
        dc.SetFont(osd_typeface(OsdTypeRole::Button));
        int ty = text_vcenter_y(dc, 10, 28);
        dc.DrawText(label, x + 26, ty);
    };
    wxString refresh_lab = tr("Refresh");
    wxString theme_lab = tr("Theme");
    theme_w_ = measure_top_btn(theme_lab);
    refresh_w_ = measure_top_btn(refresh_lab);
    theme_x_ = sz.x - PAD - theme_w_;
    refresh_x_ = theme_x_ - 8 - refresh_w_;
    draw_top_btn(refresh_x_, refresh_w_, refresh_lab, "refresh");
    draw_top_btn(theme_x_, theme_w_, theme_lab, "palette");

    /* sideBar + editor: flush (no gap between); padding is inside each pane. */
    int side_x = PAD;
    int side_y = TOP_H + PAD;
    int side_h = sz.y - TOP_H - 2 * PAD - 22;
    draw_round_rect(dc, side_x, side_y, SIDEBAR_W, side_h, 4, pal.sidebar_bg, &pal.border);

    dc.SetFont(osd_typeface(OsdTypeRole::Sidebar));
    const auto& cats = frame_->visible_cats();
    for (int v = 0; v < (int)cats.size(); v++) {
        auto cat = (VcpCategory)cats[v];
        int iy = side_y + INNER_PAD + v * 34;
        bool sel = v == frame_->cat_index();
        if (sel) {
            draw_round_rect(dc, side_x + INNER_PAD / 2, iy, SIDEBAR_W - INNER_PAD, 30, 3,
                            pal.selection_bg);
        }
        wxColour row_fg = sel ? pal.selection_fg : pal.sidebar_fg;
        wxBitmap ic = load_tabler_icon(vcp_category_icon(cat), 16, row_fg);
        int ic_y = iy + (30 - 16) / 2;
        if (ic.IsOk()) {
            dc.DrawBitmap(ic, side_x + INNER_PAD, ic_y, true);
        }
        dc.SetTextForeground(row_fg);
        wxString lab = wxString::Format("%d  %s", v + 1, tr(vcp_category_name(cat)));
        dc.DrawText(lab, side_x + INNER_PAD + 22, text_vcenter_y(dc, iy, 30));
    }

    /* editor / content pane — abut sidebar (no inter-pane margin) */
    int cx = side_x + SIDEBAR_W;
    int cy = side_y;
    int cw = sz.x - cx - PAD;
    int ch = side_h;
    draw_round_rect(dc, cx, cy, cw, ch, 4, pal.editor_bg, &pal.border);

    auto cat = cats.empty() ? VcpCategory::AppOsd : (VcpCategory)cats[frame_->cat_index()];
    int y = cy + INNER_PAD;
    auto& items = frame_->items();

    for (int i = 0; i < (int)items.size(); i++) {
        OsdItem& it = items[i];
        VcpCategory icat = it.feat ? it.feat->cat : VcpCategory::AppOsd;
        if (icat != cat) {
            continue;
        }
        if (y + ROW_H > cy + ch - INNER_PAD) {
            break;
        }
        it.row_y = y;
        it.left_x0 = it.left_x1 = it.right_x0 = it.right_x1 = 0;
        it.undo_x0 = it.undo_x1 = it.reset_x0 = it.reset_x1 = 0;
        bool active = i == frame_->active_item();
        if (active) {
            draw_round_rect(dc, cx + INNER_PAD / 2, y, cw - INNER_PAD, ROW_H - 2, 3, pal.panel_bg,
                            &pal.highlight);
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
            it.btn_x0 = cx + INNER_PAD;
            it.btn_x1 = cx + cw - INNER_PAD;
            it.bar_y0 = y + 4;
            it.bar_y1 = y + ROW_H - 8;
            draw_round_rect(dc, (int)it.btn_x0, (int)it.bar_y0, (int)(it.btn_x1 - it.btn_x0),
                            (int)(it.bar_y1 - it.bar_y0), 4,
                            active ? pal.button_bg : pal.input_bg, &pal.button_border);
            if (!icon_stem.empty()) {
                wxBitmap ic = load_tabler_icon(icon_stem, 16,
                                              active ? pal.button_fg : icon_tint);
                if (ic.IsOk()) {
                    dc.DrawBitmap(ic, (int)it.btn_x0 + 10, icon_y, true);
                }
            }
            dc.SetTextForeground(active ? pal.button_fg : pal.editor_fg);
            dc.SetFont(osd_typeface(OsdTypeRole::Button));
            dc.DrawText(name, (int)it.btn_x0 + 34, row_ty);
            y += ROW_H;
            continue;
        }

        if (!icon_stem.empty()) {
            wxBitmap ic =
                load_tabler_icon(icon_stem, 16, active ? pal.highlight : icon_tint);
            if (ic.IsOk()) {
                dc.DrawBitmap(ic, cx + INNER_PAD, icon_y, true);
            }
        }
        dc.SetTextForeground(active ? pal.highlight : pal.editor_fg);
        dc.SetFont(osd_typeface(OsdTypeRole::Label));
        dc.DrawText(name, cx + INNER_PAD + 22, row_ty);

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
            dc.SetFont(osd_typeface(OsdTypeRole::Enum));
            wxCoord vw = 0, vh = 0;
            dc.GetTextExtent(vlab, &vw, &vh);
            int value_w = std::max(48, (int)vw + 8);
            int ctrl_w = ARROW_W + 4 + value_w + 4 + ARROW_W;
            int ctrl_x = cx + cw - INNER_PAD - actions - ctrl_w;
            if (ctrl_x < cx + INNER_PAD + label_w) {
                ctrl_x = cx + INNER_PAD + label_w;
            }
            it.left_x0 = ctrl_x;
            it.left_x1 = ctrl_x + ARROW_W;
            it.bar_x0 = it.left_x1 + 4;
            it.bar_x1 = it.bar_x0 + value_w;
            it.right_x0 = it.bar_x1 + 4;
            it.right_x1 = it.right_x0 + ARROW_W;

            bool can_prev = it.choice_idx > 0;
            bool can_next = it.choice_idx >= 0 && it.choice_idx + 1 < (int)it.choices.size();
            /* dropdown chip behind enum value */
            draw_round_rect(dc, (int)it.bar_x0 - 2, (int)it.bar_y0 - 2,
                            (int)(it.bar_x1 - it.bar_x0) + 4, 20, 3, pal.dropdown_bg,
                            &pal.input_border);
            dc.SetFont(osd_typeface(OsdTypeRole::Enum));
            dc.SetTextForeground(can_prev ? pal.editor_fg : pal.muted_fg);
            dc.DrawText("<", (int)it.left_x0 + 4, row_ty);
            dc.SetTextForeground(pal.dropdown_fg);
            dc.DrawText(vlab, (int)it.bar_x0 + 4, row_ty);
            dc.SetTextForeground(can_next ? pal.editor_fg : pal.muted_fg);
            dc.DrawText(">", (int)it.right_x0 + 4, row_ty);
        } else {
            it.bar_x0 = cx + INNER_PAD + label_w;
            it.bar_x1 = cx + cw - INNER_PAD - actions - 64;
            if (it.bar_x1 < it.bar_x0 + 40) {
                it.bar_x1 = it.bar_x0 + 40;
            }
            double maxv = it.max ? it.max : 100;
            double frac = maxv > 0 ? it.current / maxv : 0;
            frac = std::clamp(frac, 0.0, 1.0);
            draw_round_rect(dc, (int)it.bar_x0, (int)it.bar_y0, (int)(it.bar_x1 - it.bar_x0), 16, 3,
                            pal.bar_track, &pal.input_border);
            draw_round_rect(dc, (int)it.bar_x0, (int)it.bar_y0,
                            (int)((it.bar_x1 - it.bar_x0) * frac), 16, 3,
                            active ? pal.highlight : pal.bar_fill);

            char vbuf[40];
            snprintf(vbuf, sizeof(vbuf), "%u", (unsigned)it.current);
            dc.SetTextForeground(pal.input_fg);
            dc.SetFont(osd_typeface(OsdTypeRole::Value));
            dc.DrawText(vbuf, (int)it.bar_x1 + 8, row_ty);
        }

        if (it.kind != VcpKind::Ro) {
            double ux = cx + cw - INNER_PAD - 2 * BTN_W - 4;
            double rx = cx + cw - INNER_PAD - BTN_W;
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

    /* statusBar */
    draw_round_rect(dc, 2, sz.y - 22, sz.x - 4, 20, 0, pal.status_bg);
    dc.SetTextForeground(pal.status_fg);
    dc.SetFont(osd_typeface(OsdTypeRole::Status));
    dc.DrawText(frame_->status(), PAD, sz.y - 18);
}

int OsdCanvas::hit_top_btn(int x, int y) const {
    if (y >= 10 && y <= 38) {
        if (refresh_w_ > 0 && x >= refresh_x_ && x <= refresh_x_ + refresh_w_) {
            return 0; /* refresh */
        }
        if (theme_w_ > 0 && x >= theme_x_ && x <= theme_x_ + theme_w_) {
            return 1; /* theme */
        }
    }
    return -1;
}

int OsdCanvas::hit_sidebar(int x, int y) const {
    int side_x = PAD;
    int side_y = TOP_H + PAD;
    if (x < side_x || x >= side_x + SIDEBAR_W) {
        return -1;
    }
    for (int v = 0; v < (int)frame_->visible_cats().size(); v++) {
        int iy = side_y + INNER_PAD + v * 34;
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

void OsdCanvas::begin_win_drag(const wxMouseEvent& ev) {
    win_drag_ = true;
    if (margin_drag_) {
        SetCursor(wxNullCursor);
    }
    margin_drag_ = false;
    margin_app_id_ = -1;
    margin_handlers_enabled_ = false; /* no margin slider / pin while moving OSD */
    win_drag_origin_ = frame_->GetPosition();
    win_drag_start_ = ev.GetPosition();
    ClientToScreen(&win_drag_start_.x, &win_drag_start_.y);
    frame_->sync_margins_from_position(win_drag_origin_, false);
    if (!HasCapture()) {
        CaptureMouse();
    }
}

bool OsdCanvas::slider_thumb_client(int item_idx, uint16_t value, wxPoint* out) const {
    if (!out || item_idx < 0 || item_idx >= (int)frame_->items().size()) {
        return false;
    }
    const OsdItem& it = frame_->items()[item_idx];
    if (it.bar_x1 <= it.bar_x0) {
        return false;
    }
    double maxv = it.max ? it.max : 100;
    double frac = maxv > 0 ? (double)value / maxv : 0.0;
    frac = std::clamp(frac, 0.0, 1.0);
    out->x = (int)std::lround(it.bar_x0 + frac * (it.bar_x1 - it.bar_x0));
    out->y = (int)std::lround((it.bar_y0 + it.bar_y1) / 2.0);
    return true;
}

void OsdCanvas::pin_cursor_to_slider(int item_idx, uint16_t value, const wxPoint* frame_origin) {
    wxPoint thumb;
    if (!slider_thumb_client(item_idx, value, &thumb)) {
        return;
    }
    /* Absolute screen from intended frame origin — X11 Move is async, so
       WarpPointer(client) against a stale window spot lands off the bar. */
    wxPoint origin = frame_origin ? *frame_origin : frame_->GetPosition();
    wxPoint screen = origin + frame_->GetClientAreaOrigin() + GetPosition() + thumb;
    wxUIActionSimulator sim;
    if (!sim.MouseMove(screen)) {
        /* Fallback: client warp relative to wherever the frame reports now. */
        wxPoint local = screen - (frame_->GetPosition() + frame_->GetClientAreaOrigin() + GetPosition());
        WarpPointer(local.x, local.y);
    }
}

void OsdCanvas::rebaseline_margin_drag(uint16_t value) {
    if (!margin_drag_) {
        return;
    }
    margin_drag_screen0_ = wxGetMousePosition();
    margin_drag_value0_ = value;
}

void OsdCanvas::on_left_down(wxMouseEvent& ev) {
    if (!mouse_events_enabled_) {
        return;
    }
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
        /* Title bar blank (outside Refresh/Theme) → drag window. */
        begin_win_drag(ev);
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
        /* Any blank area (padding, empty content) moves the OSD. */
        begin_win_drag(ev);
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
        uint16_t maxv = it.max ? it.max : 100;
        if (is_margin_app(it.app_id)) {
            if (!margin_handlers_enabled_) {
                begin_win_drag(ev);
                return;
            }
            /* Grab at current value; hide pointer and pin onto the thumb. */
            margin_drag_ = true;
            it.hist.push(it.current);
            drag_item_ = idx;
            drag_hist_pushed_ = true;
            margin_app_id_ = it.app_id;
            margin_drag_bar_w_ = std::max(1, (int)(it.bar_x1 - it.bar_x0));
            margin_drag_max_ = maxv;
            margin_drag_value0_ = it.current;
            SetCursor(wxCursor(wxCURSOR_BLANK));
            if (!HasCapture()) {
                CaptureMouse();
            }
            set_mouse_events_enabled(false);
            pin_cursor_to_slider(idx, it.current);
            rebaseline_margin_drag(it.current);
            set_mouse_events_enabled(true);
            return;
        }
        margin_drag_ = false;
        double frac = (ev.GetX() - it.bar_x0) / (it.bar_x1 - it.bar_x0);
        uint16_t v = (uint16_t)std::lround(frac * maxv);
        it.hist.push(it.current);
        drag_item_ = idx;
        drag_hist_pushed_ = true;
        if (!HasCapture()) {
            CaptureMouse();
        }
        frame_->queue_set_item(idx, v, true, false);
        return;
    }
    /* Row label / non-control chrome → drag window. */
    begin_win_drag(ev);
}

void OsdCanvas::on_left_up(wxMouseEvent&) {
    if (!mouse_events_enabled_) {
        return;
    }
    if (win_drag_) {
        win_drag_ = false;
        frame_->sync_margins_from_position(frame_->GetPosition(), true);
        margin_handlers_enabled_ = true;
    }
    if (margin_drag_) {
        SetCursor(wxNullCursor);
    }
    drag_item_ = -1;
    drag_hist_pushed_ = false;
    margin_drag_ = false;
    margin_app_id_ = -1;
    if (HasCapture()) {
        ReleaseMouse();
    }
}

void OsdCanvas::on_motion(wxMouseEvent& ev) {
    if (!mouse_events_enabled_) {
        return;
    }
    if (win_drag_ && ev.Dragging() && ev.LeftIsDown()) {
        wxPoint cur = ev.GetPosition();
        ClientToScreen(&cur.x, &cur.y);
        wxPoint pos = win_drag_origin_ + (cur - win_drag_start_);
        int w = 0, h = 0;
        frame_->GetSize(&w, &h);
        /* Keep the OSD inside the monitor under the pointer (not virtual desktop). */
        wxRect geo = frame_->monitor_client_at(wxGetMousePosition());
        if (w > 0 && h > 0 && geo.width > 0 && geo.height > 0) {
            pos.x = std::clamp(pos.x, geo.x, geo.x + std::max(0, geo.width - w));
            pos.y = std::clamp(pos.y, geo.y, geo.y + std::max(0, geo.height - h));
        }
        frame_->Move(pos);
        frame_->sync_margins_from_position(pos, false);
        return;
    }
    if (drag_item_ < 0 || !ev.LeftIsDown()) {
        return;
    }
    OsdItem& it = frame_->items()[drag_item_];
    if (it.kind != VcpKind::Continuous) {
        return;
    }
    uint16_t v;
    if (margin_drag_) {
        if (!margin_handlers_enabled_) {
            return;
        }
        /* Screen Δx; window Move + thumb pin naturally invert right-edge feel. */
        int delta = wxGetMousePosition().x - margin_drag_screen0_.x;
        int iv = (int)margin_drag_value0_ +
                 (int)std::lround((double)delta * (double)margin_drag_max_ /
                                  (double)margin_drag_bar_w_);
        if (iv < 0) {
            iv = 0;
        }
        if (iv > (int)margin_drag_max_) {
            iv = margin_drag_max_;
        }
        v = (uint16_t)iv;
    } else {
        double frac = (ev.GetX() - it.bar_x0) / (it.bar_x1 - it.bar_x0);
        frac = std::clamp(frac, 0.0, 1.0);
        uint16_t maxv = it.max ? it.max : 100;
        v = (uint16_t)std::lround(frac * maxv);
    }
    frame_->queue_set_item(drag_item_, v, true, false);
}

void OsdCanvas::on_mousewheel(wxMouseEvent& ev) {
    if (!mouse_events_enabled_) {
        return;
    }
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
    if (key == WXK_F11) {
        frame_->cycle_theme(-1);
        return;
    }
    if (key == WXK_F12) {
        frame_->cycle_theme(1);
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
