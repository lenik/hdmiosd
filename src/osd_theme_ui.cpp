/*
 * Copyright (C) 2026 Lenik <hdmiosd@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "osd_frame.hpp"

#include "osd_draw.hpp"

#include <bas/locale/i18n.h>

#include <wx/dc.h>
#include <wx/dcbuffer.h>
#include <wx/notifmsg.h>
#include <wx/popupwin.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {
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

