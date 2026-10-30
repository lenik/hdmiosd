/*
 * Copyright (C) 2026 Lenik <hdmiosd@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "osd_draw.hpp"

#include <bas/locale/i18n.h>

bool is_margin_app(int app_id) {
    return app_id == APP_LEFT || app_id == APP_RIGHT || app_id == APP_TOP || app_id == APP_BOTTOM;
}

wxString tr(const char* msgid) { return wxString::FromUTF8(_(msgid)); }
wxString tr(const std::string& msgid) { return wxString::FromUTF8(_(msgid.c_str())); }

void draw_round_rect(wxDC& dc, int x, int y, int w, int h, int r, const wxColour& fill,
                     const wxColour* stroke) {
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
