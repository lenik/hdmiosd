/*
 * Copyright (C) 2026 Lenik <hdmiosd@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "display.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <limits.h>
#include <unistd.h>

static std::string read_sys_file(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) {
        return {};
    }
    char buf[512];
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return {};
    }
    fclose(f);
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[--n] = '\0';
    }
    return buf;
}

static std::string realpath_str(const std::string& path) {
    char* rp = realpath(path.c_str(), nullptr);
    if (!rp) {
        return {};
    }
    std::string out = rp;
    free(rp);
    return out;
}

static void edid_mfg(const uint8_t edid[128], std::string& mfg) {
    int id = (edid[8] << 8) | edid[9];
    char b[4] = {(char)('A' + ((id >> 10) & 0x1f) - 1), (char)('A' + ((id >> 5) & 0x1f) - 1),
                 (char)('A' + (id & 0x1f) - 1), 0};
    mfg = b;
}

static void edid_text(const uint8_t* desc, std::string& out) {
    out.clear();
    for (int i = 5; i < 18; i++) {
        char c = (char)desc[i];
        if (c == '\n' || c == '\0') {
            break;
        }
        if (c >= 32 && c < 127) {
            out.push_back(c);
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
}

bool HdmiDisplay::open() {
    if (bus.is_open()) {
        return true;
    }
    return bus.open(i2c_path) == DdcErr::Ok;
}

void HdmiDisplay::close() { bus.close(); }

std::string HdmiDisplay::label() const {
    const char* mod = model.empty() ? "Display" : model.c_str();
    const char* mf = mfg.empty() ? "?" : mfg.c_str();
    if (!connector.empty()) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s (%s) - %s [%s]", mod, mf, connector.c_str(),
                 i2c_path.c_str());
        return buf;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "%s (%s) [%s]", mod, mf, i2c_path.c_str());
    return buf;
}

void DisplayList::clear() {
    for (auto& d : items_) {
        d.close();
    }
    items_.clear();
}

bool DisplayList::already_have_bus(int busno) const {
    for (const auto& d : items_) {
        if (d.busno == busno) {
            return true;
        }
    }
    return false;
}

void DisplayList::adopt(HdmiDisplay&& d) {
    if (items_.size() >= DISPLAY_MAX) {
        d.close();
        return;
    }
    if (d.busno >= 0 && already_have_bus(d.busno)) {
        d.close();
        return;
    }
    d.index = (int)items_.size();
    items_.push_back(std::move(d));
}

static void fill_drm_from_busno(HdmiDisplay& d) {
    if (d.busno < 0 || !d.drm_name.empty()) {
        return;
    }
    DIR* dir = opendir("/sys/class/drm");
    if (!dir) {
        return;
    }
    char want[32];
    snprintf(want, sizeof(want), "i2c-%d", d.busno);
    while (dirent* ent = readdir(dir)) {
        if (strncmp(ent->d_name, "card", 4) != 0 || !strchr(ent->d_name, '-')) {
            continue;
        }
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/sys/class/drm/%s/%s", ent->d_name, want);
        if (access(path, F_OK) != 0) {
            continue;
        }
        d.drm_name = ent->d_name;
        if (const char* cn = strchr(ent->d_name, '-')) {
            d.connector = cn + 1;
        }
        break;
    }
    closedir(dir);
}

bool DisplayList::probe_candidate(const I2cCandidate& c, HdmiDisplay& out) {
    out = HdmiDisplay{};
    out.i2c_path = c.i2c_path;
    out.drm_name = c.drm_name;
    auto dash = c.i2c_path.rfind('-');
    if (dash != std::string::npos) {
        out.busno = atoi(c.i2c_path.c_str() + dash + 1);
    }
    if (!c.drm_name.empty()) {
        if (auto* cn = strchr(c.drm_name.c_str(), '-')) {
            out.connector = cn + 1;
        }
    } else {
        fill_drm_from_busno(out);
    }

    if (out.bus.open(c.i2c_path) != DdcErr::Ok) {
        return false;
    }
    if (out.bus.read_edid(out.edid) == DdcErr::Ok) {
        out.edid_ok = true;
        edid_mfg(out.edid, out.mfg);
        for (int i = 0; i < 4; i++) {
            const uint8_t* desc = &out.edid[54 + i * 18];
            if (desc[0] || desc[1] || desc[2]) {
                continue;
            }
            if (desc[3] == 0xfc) {
                edid_text(desc, out.model);
            } else if (desc[3] == 0xff) {
                edid_text(desc, out.serial);
            }
        }
    }
    if (out.bus.detect() != DdcErr::Ok) {
        out.bus.close();
        return false;
    }
    out.ddc_ok = true;
    std::string caps;
    if (out.bus.capabilities(caps) > 0) {
        vcp_caps_parse(out.caps, caps);
    }
    out.bus.close();
    return true;
}

int DisplayList::add_i2c_candidate(const std::string& i2c_path, const std::string& drm_name) {
    if (items_.size() >= DISPLAY_MAX) {
        return -1;
    }
    int busno = -1;
    auto dash = i2c_path.rfind('-');
    if (dash != std::string::npos) {
        busno = atoi(i2c_path.c_str() + dash + 1);
    }
    if (busno >= 0 && already_have_bus(busno)) {
        return 0;
    }
    HdmiDisplay d;
    if (!probe_candidate({i2c_path, drm_name}, d)) {
        return 0;
    }
    if (d.busno >= 0 && already_have_bus(d.busno)) {
        d.close();
        return 0;
    }
    d.index = (int)items_.size();
    items_.push_back(std::move(d));
    return 1;
}

void DisplayList::collect_drm(std::vector<I2cCandidate>& out, const std::string& only_name) {
    DIR* dir = opendir("/sys/class/drm");
    if (!dir) {
        return;
    }
    while (dirent* ent = readdir(dir)) {
        if (strncmp(ent->d_name, "card", 4) != 0 || !strchr(ent->d_name, '-')) {
            continue;
        }
        if (!only_name.empty() && only_name != ent->d_name &&
            !strstr(ent->d_name, only_name.c_str())) {
            continue;
        }
        std::string base = std::string("/sys/class/drm/") + ent->d_name;
        std::string status = read_sys_file(base + "/status");
        if (!status.empty() && status != "connected") {
            continue;
        }
        std::string i2c_path;
        DIR* sub = opendir(base.c_str());
        if (sub) {
            while (dirent* e2 = readdir(sub)) {
                if (strncmp(e2->d_name, "i2c-", 4) == 0) {
                    i2c_path = std::string("/dev/") + e2->d_name;
                    break;
                }
            }
            closedir(sub);
        }
        if (i2c_path.empty()) {
            std::string resolved = realpath_str(base + "/ddc");
            auto slash = resolved.rfind('/');
            if (slash != std::string::npos && resolved.compare(slash + 1, 4, "i2c-") == 0) {
                i2c_path = std::string("/dev/") + resolved.substr(slash + 1);
            }
        }
        if (!i2c_path.empty()) {
            out.push_back({i2c_path, ent->d_name});
        }
    }
    closedir(dir);
}

void DisplayList::collect_all_i2c(std::vector<I2cCandidate>& out) {
    DIR* dir = opendir("/dev");
    if (!dir) {
        return;
    }
    while (dirent* ent = readdir(dir)) {
        if (strncmp(ent->d_name, "i2c-", 4) == 0) {
            out.push_back({std::string("/dev/") + ent->d_name, {}});
        }
    }
    closedir(dir);
}

void DisplayList::collect_from_filter(std::vector<I2cCandidate>& out, const std::string& filter) {
    if (filter.rfind("/dev/i2c-", 0) == 0) {
        out.push_back({filter, {}});
        return;
    }
    std::string use = filter;
    if (!filter.empty() && filter[0] == '/') {
        std::string rp = realpath_str(filter);
        if (!rp.empty()) {
            use = rp;
        }
    }
    DIR* dir = opendir("/sys/class/drm");
    if (!dir) {
        return;
    }
    while (dirent* ent = readdir(dir)) {
        if (strncmp(ent->d_name, "card", 4) != 0 || !strchr(ent->d_name, '-')) {
            continue;
        }
        std::string devpath =
            realpath_str(std::string("/sys/class/drm/") + ent->d_name + "/device");
        if (devpath.find(use) == std::string::npos &&
            std::string(ent->d_name).find(use) == std::string::npos &&
            std::string(ent->d_name).find(filter) == std::string::npos) {
            continue;
        }
        collect_drm(out, ent->d_name);
    }
    closedir(dir);
}

void DisplayList::dedupe_candidates(std::vector<I2cCandidate>& cands) {
    std::vector<I2cCandidate> uniq;
    for (auto& c : cands) {
        bool seen = false;
        for (const auto& u : uniq) {
            if (u.i2c_path == c.i2c_path) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            uniq.push_back(std::move(c));
        }
    }
    cands = std::move(uniq);
}

std::vector<I2cCandidate> DisplayList::enumerate_candidates(const std::string& filter_dev) {
    std::vector<I2cCandidate> out;
    if (!filter_dev.empty()) {
        collect_from_filter(out, filter_dev);
        if (out.empty()) {
            std::vector<I2cCandidate> all;
            collect_drm(all, {});
            if (all.empty()) {
                collect_all_i2c(all);
            }
            for (auto& c : all) {
                if (c.i2c_path.find(filter_dev) != std::string::npos ||
                    c.drm_name.find(filter_dev) != std::string::npos) {
                    out.push_back(std::move(c));
                }
            }
        }
    } else {
        collect_drm(out, {});
        if (out.empty()) {
            collect_all_i2c(out);
        }
    }
    dedupe_candidates(out);
    return out;
}

void DisplayList::scan_drm(const std::string& only_name) {
    std::vector<I2cCandidate> cands;
    collect_drm(cands, only_name);
    for (const auto& c : cands) {
        add_i2c_candidate(c.i2c_path, c.drm_name);
    }
}

void DisplayList::scan_all_i2c() {
    std::vector<I2cCandidate> cands;
    collect_all_i2c(cands);
    for (const auto& c : cands) {
        add_i2c_candidate(c.i2c_path, c.drm_name);
    }
}

void DisplayList::discover_from_filter(const std::string& filter) {
    std::vector<I2cCandidate> cands;
    collect_from_filter(cands, filter);
    for (const auto& c : cands) {
        add_i2c_candidate(c.i2c_path, c.drm_name);
    }
}

int DisplayList::discover(const std::string& filter_dev) {
    clear();
    auto cands = enumerate_candidates(filter_dev);
    for (const auto& c : cands) {
        add_i2c_candidate(c.i2c_path, c.drm_name);
    }
    /* Filter may have matched path/model after probe — re-filter if needed. */
    if (!filter_dev.empty() && !items_.empty()) {
        bool any_path_match = false;
        for (const auto& d : items_) {
            if (d.i2c_path.find(filter_dev) != std::string::npos ||
                d.drm_name.find(filter_dev) != std::string::npos ||
                d.connector.find(filter_dev) != std::string::npos ||
                d.model.find(filter_dev) != std::string::npos) {
                any_path_match = true;
                break;
            }
        }
        if (any_path_match) {
            std::vector<HdmiDisplay> keep;
            for (auto& d : items_) {
                if (d.i2c_path.find(filter_dev) != std::string::npos ||
                    d.drm_name.find(filter_dev) != std::string::npos ||
                    d.connector.find(filter_dev) != std::string::npos ||
                    d.model.find(filter_dev) != std::string::npos) {
                    d.index = (int)keep.size();
                    keep.push_back(std::move(d));
                } else {
                    d.close();
                }
            }
            items_ = std::move(keep);
        }
    }
    return (int)items_.size();
}
