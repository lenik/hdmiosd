#ifndef HDMIOOSD_DDC_HPP
#define HDMIOOSD_DDC_HPP

#include <cstdint>
#include <string>

constexpr uint8_t DDC_ADDR_7BIT = 0x37;
constexpr uint8_t DDC_EDID_ADDR = 0x50;

enum class DdcErr {
    Ok = 0,
    Io = -1,
    Open = -2,
    Checksum = -3,
    Protocol = -4,
    Unsupported = -5,
    Timeout = -6,
};

struct DdcVcpValue {
    uint8_t code = 0;
    uint8_t type = 0;
    uint16_t current = 0;
    uint16_t max = 0;
    bool valid = false;
};

class DdcBus {
  public:
    DdcBus() = default;
    ~DdcBus() { close(); }

    DdcBus(const DdcBus&) = delete;
    DdcBus& operator=(const DdcBus&) = delete;

    DdcBus(DdcBus&& o) noexcept : fd_(o.fd_), path_(std::move(o.path_)), busno_(o.busno_) {
        o.fd_ = -1;
    }
    DdcBus& operator=(DdcBus&& o) noexcept {
        if (this != &o) {
            close();
            fd_ = o.fd_;
            path_ = std::move(o.path_);
            busno_ = o.busno_;
            o.fd_ = -1;
        }
        return *this;
    }

    DdcErr open(const std::string& i2c_path);
    void close();
    bool is_open() const { return fd_ >= 0; }

    DdcErr detect();
    DdcErr get_vcp(uint8_t code, DdcVcpValue& out);
    DdcErr set_vcp(uint8_t code, uint16_t value);
    DdcErr save_settings();
    int capabilities(std::string& out);
    DdcErr read_edid(uint8_t edid[128]);

    const std::string& path() const { return path_; }
    int busno() const { return busno_; }

    static const char* strerror(DdcErr err);

  private:
    int fd_ = -1;
    std::string path_;
    int busno_ = -1;
};

#endif
