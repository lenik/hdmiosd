/*
 * Copyright (C) 2026 Lenik <hdmiosd@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "osd_frame.hpp"

#include "osd_draw.hpp"

#include <bas/locale/i18n.h>
#include <bas/log/uselog.h>

#include <wx/display.h>

#include <algorithm>
#include <cstdio>

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
            if (visible_cats_[(size_t)i] == (int)VcpCategory::SoftOsd) {
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
