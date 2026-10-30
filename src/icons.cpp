/*
 * Copyright (C) 2026 Lenik <hdmiosd@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "icons.hpp"

#include <bas/proc/AssetsRegistry.hpp>
#include <bas/volume/Volume.hpp>
#include <bas/wx/images.hpp>

#include <wx/image.h>

static wxBitmap tint_bitmap(const wxBitmap& src, const wxColour& tint) {
    if (!src.IsOk()) {
        return {};
    }
    wxImage img = src.ConvertToImage();
    if (!img.HasAlpha()) {
        img.InitAlpha();
    }
    unsigned char tr = tint.Red(), tg = tint.Green(), tb = tint.Blue();
    int w = img.GetWidth(), h = img.GetHeight();
    unsigned char* data = img.GetData();
    unsigned char* alpha = img.GetAlpha();
    for (int i = 0; i < w * h; i++) {
        /* Keep alpha; replace RGB with tint (Tabler PNGs are dark glyphs). */
        data[i * 3 + 0] = tr;
        data[i * 3 + 1] = tg;
        data[i * 3 + 2] = tb;
        (void)alpha;
    }
    return wxBitmap(img);
}

wxBitmap load_tabler_icon(const std::string& stem, int size, const wxColour& tint) {
    if (stem.empty()) {
        return {};
    }
    std::string path = (size <= 16) ? ("icons/16/" + stem + ".png") : ("icons/24/" + stem + ".png");
    auto bmp = imageLoadAsset(path, size, size);
    if (!bmp) {
        path = (size <= 16) ? ("icons/24/" + stem + ".png") : ("icons/16/" + stem + ".png");
        bmp = imageLoadAsset(path, size, size);
    }
    if (!bmp) {
        return {};
    }
    return tint_bitmap(*bmp, tint);
}

wxBitmap load_app_icon(int size) {
    auto bmp = imageLoadAsset("icons/hdmiosd.svg", size, size);
    if (bmp) {
        return *bmp;
    }
    /* Fallback: compose from tabler */
    return load_tabler_icon("device-desktop", size, wxColour(255, 255, 255));
}
