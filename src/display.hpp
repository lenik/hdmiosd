#ifndef HDMIOOSD_DISPLAY_HPP
#define HDMIOOSD_DISPLAY_HPP

#include "ddc.hpp"
#include "vcp.hpp"

#include <cstddef>
#include <string>
#include <vector>

constexpr size_t DISPLAY_MAX = 32;

struct HdmiDisplay {
    int index = 0;
    std::string i2c_path;
    int busno = -1;
    std::string drm_name;
    std::string connector;
    std::string mfg;
    std::string model;
    std::string serial;
    uint8_t edid[128]{};
    bool edid_ok = false;
    bool ddc_ok = false;
    VcpCaps caps;
    DdcBus bus;

    bool open();
    void close();
    std::string label() const;
};

/* Filesystem-only candidate (no I2C I/O). */
struct I2cCandidate {
    std::string i2c_path;
    std::string drm_name;
};

class DisplayList {
  public:
    void clear();
    /* Blocking full discover (CLI --list). */
    int discover(const std::string& filter_dev = {});

    /* Fast path: DRM/sysfs + /dev names only. */
    static std::vector<I2cCandidate> enumerate_candidates(const std::string& filter_dev = {});
    /* Slow path: EDID + DDC detect + capabilities (safe off UI thread). */
    static bool probe_candidate(const I2cCandidate& c, HdmiDisplay& out);

    void adopt(HdmiDisplay&& d);
    size_t size() const { return items_.size(); }
    HdmiDisplay& at(size_t i) { return items_.at(i); }
    const HdmiDisplay& at(size_t i) const { return items_.at(i); }
    std::vector<HdmiDisplay>& items() { return items_; }

  private:
    std::vector<HdmiDisplay> items_;
    bool already_have_bus(int busno) const;
    int add_i2c_candidate(const std::string& i2c_path, const std::string& drm_name);
    void scan_drm(const std::string& only_name);
    void scan_all_i2c();
    void discover_from_filter(const std::string& filter);

    static void collect_drm(std::vector<I2cCandidate>& out, const std::string& only_name);
    static void collect_all_i2c(std::vector<I2cCandidate>& out);
    static void collect_from_filter(std::vector<I2cCandidate>& out, const std::string& filter);
    static void dedupe_candidates(std::vector<I2cCandidate>& cands);
};

#endif
