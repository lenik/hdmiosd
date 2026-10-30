#ifndef HDMIOOSD_OSD_DRAW_HPP
#define HDMIOOSD_OSD_DRAW_HPP

#include <wx/colour.h>
#include <wx/dc.h>
#include <wx/string.h>

#include <string>

constexpr int SIDEBAR_W = 168;
constexpr int ROW_H = 36;
constexpr int TOP_H = 48;
constexpr int STATUS_H = 22;
constexpr int INFO_H = 72; /* content-panel description strip */
constexpr int BTN_W = 28;
constexpr int PAD = 10;       /* title/status text & button inset only */
constexpr int INNER_PAD = 8;  /* padding inside sidebar / content panes */
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

bool is_margin_app(int app_id);

wxString tr(const char* msgid);
wxString tr(const std::string& msgid);

void draw_round_rect(wxDC& dc, int x, int y, int w, int h, int r, const wxColour& fill,
                     const wxColour* stroke = nullptr);
int text_vcenter_y(wxDC& dc, int row_y, int row_h);

#endif
