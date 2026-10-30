#ifndef HDMIOOSD_ICONS_HPP
#define HDMIOOSD_ICONS_HPP

#include <wx/bitmap.h>
#include <wx/colour.h>

#include <string>

/* Load Tabler icon from embedded assets and tint to colour (keeps alpha). */
wxBitmap load_tabler_icon(const std::string& stem, int size = 24,
                          const wxColour& tint = wxColour(255, 255, 255));

/* App icon from embedded SVG. */
wxBitmap load_app_icon(int size = 64);

#endif
