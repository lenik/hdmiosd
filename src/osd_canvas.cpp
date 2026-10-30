/*
 * Copyright (C) 2026 Lenik <hdmiosd@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "osd_frame.hpp"

#include "osd_draw.hpp"
#include "icons.hpp"
#include "config.h"

#include <bas/locale/i18n.h>

#include <wx/cursor.h>
#include <wx/dc.h>
#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/uiaction.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

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
    drag_item_ = -1;
    margin_app_id_ = -1;
    margin_handlers_enabled_ = false; /* no margin slider / pin while moving OSD */
    frame_->set_window_dragging(true);
    win_drag_origin_ = frame_->GetPosition();
    win_drag_start_ = wxGetMousePosition();
    (void)ev;
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
        frame_->set_window_dragging(false);
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
    if (win_drag_ && ev.LeftIsDown()) {
        /* Screen coords only — client coords jump when the frame Moves under us. */
        wxPoint cur = wxGetMousePosition();
        wxPoint pos = win_drag_origin_ + (cur - win_drag_start_);
        int w = 0, h = 0;
        frame_->GetSize(&w, &h);
        /* Keep the OSD inside the monitor under the pointer (not virtual desktop). */
        wxRect geo = frame_->monitor_client_at(cur);
        if (w > 0 && h > 0 && geo.width > 0 && geo.height > 0) {
            pos.x = std::clamp(pos.x, geo.x, geo.x + std::max(0, geo.width - w));
            pos.y = std::clamp(pos.y, geo.y, geo.y + std::max(0, geo.height - h));
        }
        frame_->Move(pos);
        /* Update margin values/highlight only — never apply_window_placement. */
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
