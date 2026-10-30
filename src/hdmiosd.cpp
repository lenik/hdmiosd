/*
 * Copyright (C) 2026 Lenik <hdmiosd@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "display.hpp"
#include "osd_frame.hpp"
#include "settings.hpp"

#include <bas/locale/i18n.h>
#include <bas/log/uselog.h>

extern "C" {
#include <bas/proc/env.h>
}

#include <wx/app.h>
#include <wx/image.h>
#include <wx/msgdlg.h>

#include <cstdio>
#include <cstring>
#include <getopt.h>
#include <string>

enum { OPT_VERSION = 256 };

static void usage(FILE* out) {
    fputs(_("Usage: hdmiosd [OPTION]...\n"
            "Control HDMI/DP monitor settings via DDC/CI (Device OSD) and App OSD.\n\n"),
          out);
    fputs("  -d, --device DEV   ", out);
    fputs(_("/dev/i2c-*, /dev/video*, PCI/USB id, or /sys/... path\n"), out);
    fputs("  -o, --opacity NUM  ", out);
    fputs(_("window opacity 0-100 (overrides settings.ini)\n"), out);
    fputs("  -g, --geom W[xH]   ", out);
    fputs(_("window size\n"), out);
    fputs("  -l, --left NUM     ", out);
    fputs(_("place from left edge\n"), out);
    fputs("  -r, --right NUM    ", out);
    fputs(_("place from right edge\n"), out);
    fputs("  -t, --top NUM      ", out);
    fputs(_("place from top edge\n"), out);
    fputs("  -b, --bottom NUM   ", out);
    fputs(_("place from bottom edge\n"), out);
    fputs("  -L, --list         ", out);
    fputs(_("list DDC-capable displays and exit\n"), out);
    fputs("  -v, --verbose      ", out);
    fputs(_("more logging\n"), out);
    fputs("  -q, --quiet        ", out);
    fputs(_("less logging\n"), out);
    fputs("  -h, --help         ", out);
    fputs(_("help\n"), out);
    fputs("      --version      ", out);
    fputs(_("version\n\n"), out);
    fprintf(out, _("Settings: %s\n"), Settings::config_path().c_str());
    fprintf(out, _("Report bugs to: <%s>\n"), PROJECT_EMAIL);
}

static int parse_geom(const char* s, int* w, int* h) {
    int a = 0, b = 0;
    if (sscanf(s, "%dx%d", &a, &b) == 2 || sscanf(s, "%dX%d", &a, &b) == 2) {
        *w = a;
        *h = b;
        return 0;
    }
    if (sscanf(s, "%d", &a) == 1) {
        *w = a;
        *h = 0;
        return 0;
    }
    return -1;
}

class HdmiosdApp : public wxApp {
  public:
    DisplayList* list = nullptr;
    Settings* settings = nullptr;
    std::string filter;

    bool OnInit() override {
        if (!wxApp::OnInit()) {
            return false;
        }
        wxInitAllImageHandlers();
        /* Window shows immediately; discovery + VCP reads run in background. */
        auto* frame = new OsdFrame(list, settings, filter);
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP_NO_MAIN(HdmiosdApp);

int main(int argc, char** argv) {
    const char* exe = self_exe();
    init_i18n(LOCALEDIR);

    const char* device = nullptr;
    int list_only = 0;
    Settings settings;
    settings.load();

    bool opacity_cli = false, geom_cli = false;

    static const struct option long_opts[] = {
        {"device", required_argument, nullptr, 'd'},
        {"opacity", required_argument, nullptr, 'o'},
        {"geom", required_argument, nullptr, 'g'},
        {"left", required_argument, nullptr, 'l'},
        {"right", required_argument, nullptr, 'r'},
        {"top", required_argument, nullptr, 't'},
        {"bottom", required_argument, nullptr, 'b'},
        {"list", no_argument, nullptr, 'L'},
        {"verbose", no_argument, nullptr, 'v'},
        {"quiet", no_argument, nullptr, 'q'},
        {"help", no_argument, nullptr, 'h'},
        {"version", no_argument, nullptr, OPT_VERSION},
        {nullptr, 0, nullptr, 0},
    };

    for (;;) {
        int c = getopt_long(argc, argv, "d:o:g:l:r:t:b:Lvqh", long_opts, nullptr);
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'd':
            device = optarg;
            break;
        case 'o': {
            int pct = atoi(optarg);
            if (pct < 0) {
                pct = 0;
            }
            if (pct > 100) {
                pct = 100;
            }
            settings.app.opacity = pct / 100.0;
            opacity_cli = true;
            break;
        }
        case 'g':
            if (parse_geom(optarg, &settings.app.width, &settings.app.height) != 0) {
                fprintf(stderr, "%s: invalid geometry: %s\n", exe, optarg);
                return 1;
            }
            geom_cli = true;
            break;
        case 'l':
            settings.app.margin_left = atoi(optarg);
            settings.app.have_left = true;
            settings.app.have_right = false;
            break;
        case 'r':
            settings.app.margin_right = atoi(optarg);
            settings.app.have_right = true;
            settings.app.have_left = false;
            break;
        case 't':
            settings.app.margin_top = atoi(optarg);
            settings.app.have_top = true;
            settings.app.have_bottom = false;
            break;
        case 'b':
            settings.app.margin_bottom = atoi(optarg);
            settings.app.have_bottom = true;
            settings.app.have_top = false;
            break;
        case 'L':
            list_only = 1;
            break;
        case 'v':
            log_more();
            break;
        case 'q':
            log_less();
            break;
        case 'h':
            usage(stdout);
            return 0;
        case OPT_VERSION:
            printf("hdmiosd %s\n", PROJECT_VERSION);
            printf("Copyright (C) %d %s\n", PROJECT_YEAR, PROJECT_AUTHOR);
            puts("License AGPL-3.0-or-later");
            return 0;
        default:
            usage(stderr);
            return 1;
        }
    }

    DisplayList list;

    if (list_only) {
        int n = list.discover(device ? device : "");
        loginfo_fmt("%s: found %d DDC display(s)", exe, n);
        for (size_t i = 0; i < list.size(); i++) {
            printf("%zu: %s\n", i, list.at(i).label().c_str());
            if (!list.at(i).caps.raw.empty()) {
                printf("    caps: %.120s%s\n", list.at(i).caps.raw.c_str(),
                       list.at(i).caps.raw.size() > 120 ? "..." : "");
            }
        }
        return n > 0 ? 0 : 1;
    }

    (void)opacity_cli;
    (void)geom_cli;

    auto* inst = new HdmiosdApp();
    wxApp::SetInstance(inst);
    inst->list = &list;
    inst->settings = &settings;
    inst->filter = device ? device : "";

    /* Do not forward our getopt flags to wx's cmdline parser. */
    int wx_argc = 1;
    char* wx_argv[] = {argv[0], nullptr};
    int rc = wxEntry(wx_argc, wx_argv);
    list.clear();
    return rc;
}
