/*
 * Copyright (C) 2026 Lenik <hdmiosd@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ddc.hpp"

#include <cstring>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef I2C_SLAVE_FORCE
#define I2C_SLAVE_FORCE 0x0706
#endif

static uint8_t ddc_checksum(uint8_t init, const uint8_t* p, size_t n) {
    uint8_t c = init;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
    }
    return c;
}

static DdcErr write_raw(int fd, const uint8_t* buf, size_t len) {
    if (ioctl(fd, I2C_SLAVE_FORCE, DDC_ADDR_7BIT) < 0 && ioctl(fd, I2C_SLAVE, DDC_ADDR_7BIT) < 0) {
        return DdcErr::Io;
    }
    if (write(fd, buf, len) != (ssize_t)len) {
        return DdcErr::Io;
    }
    return DdcErr::Ok;
}

static int read_raw(int fd, uint8_t* buf, size_t len) {
    if (ioctl(fd, I2C_SLAVE_FORCE, DDC_ADDR_7BIT) < 0 && ioctl(fd, I2C_SLAVE, DDC_ADDR_7BIT) < 0) {
        return (int)DdcErr::Io;
    }
    ssize_t n = read(fd, buf, len);
    if (n < 0) {
        return (int)DdcErr::Io;
    }
    if ((size_t)n < len) {
        memset(buf + n, 0, len - (size_t)n);
    }
    return (int)n;
}

static int xfer(int fd, const uint8_t* req, size_t req_len, uint8_t* resp, size_t resp_len,
                unsigned settle_us) {
    DdcErr rc = write_raw(fd, req, req_len);
    if (rc != DdcErr::Ok) {
        return (int)rc;
    }
    usleep(settle_us);
    if (!resp || resp_len == 0) {
        return (int)DdcErr::Ok;
    }
    return read_raw(fd, resp, resp_len);
}

DdcErr DdcBus::open(const std::string& i2c_path) {
    close();
    path_ = i2c_path;
    busno_ = -1;
    auto dash = i2c_path.rfind('-');
    if (dash != std::string::npos) {
        busno_ = atoi(i2c_path.c_str() + dash + 1);
    }
    fd_ = ::open(i2c_path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
        return DdcErr::Open;
    }
#ifndef I2C_TIMEOUT
#define I2C_TIMEOUT 0x0702
#endif
#ifndef I2C_RETRIES
#define I2C_RETRIES 0x0701
#endif
    /* Keep probes snappy: timeout unit is 10 ms; avoid indefinite I2C hangs. */
    unsigned long timeout_units = 20; /* 200 ms */
    unsigned long retries = 1;
    (void)ioctl(fd_, I2C_TIMEOUT, timeout_units);
    (void)ioctl(fd_, I2C_RETRIES, retries);
    return DdcErr::Ok;
}

void DdcBus::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

DdcErr DdcBus::detect() {
    DdcVcpValue v;
    if (get_vcp(0xDF, v) == DdcErr::Ok && v.valid) {
        return DdcErr::Ok;
    }
    return (get_vcp(0x10, v) == DdcErr::Ok && v.valid) ? DdcErr::Ok : DdcErr::Unsupported;
}

DdcErr DdcBus::get_vcp(uint8_t code, DdcVcpValue& out) {
    out = {};
    if (fd_ < 0) {
        return DdcErr::Io;
    }
    uint8_t req[5] = {0x51, 0x82, 0x01, code, 0};
    req[4] = ddc_checksum(0x6e, req, 4);
    uint8_t resp[16] = {};
    int n = xfer(fd_, req, sizeof(req), resp, 11, 50000);
    if (n < 11) {
        return n < 0 ? (DdcErr)n : DdcErr::Protocol;
    }
    if (resp[0] != 0x6e || resp[2] != 0x02) {
        return DdcErr::Protocol;
    }
    if ((resp[1] & 0x7f) < 8) {
        return DdcErr::Protocol;
    }
    if (resp[3] != 0x00) {
        return DdcErr::Unsupported;
    }
    if (resp[4] != code) {
        return DdcErr::Protocol;
    }
    out.code = code;
    out.type = resp[5];
    out.max = (uint16_t)((resp[6] << 8) | resp[7]);
    out.current = (uint16_t)((resp[8] << 8) | resp[9]);
    out.valid = true;
    return DdcErr::Ok;
}

DdcErr DdcBus::set_vcp(uint8_t code, uint16_t value) {
    if (fd_ < 0) {
        return DdcErr::Io;
    }
    uint8_t req[7] = {0x51, 0x84, 0x03, code, (uint8_t)((value >> 8) & 0xff),
                      (uint8_t)(value & 0xff), 0};
    req[6] = ddc_checksum(0x6e, req, 6);
    int rc = xfer(fd_, req, sizeof(req), nullptr, 0, 50000);
    return rc < 0 ? (DdcErr)rc : DdcErr::Ok;
}

DdcErr DdcBus::save_settings() {
    if (fd_ < 0) {
        return DdcErr::Io;
    }
    uint8_t req[4] = {0x51, 0x81, 0x0c, 0};
    req[3] = ddc_checksum(0x6e, req, 3);
    int rc = xfer(fd_, req, sizeof(req), nullptr, 0, 80000);
    return rc < 0 ? (DdcErr)rc : DdcErr::Ok;
}

int DdcBus::capabilities(std::string& out) {
    out.clear();
    if (fd_ < 0) {
        return (int)DdcErr::Io;
    }
    uint16_t offset = 0;
    for (int round = 0; round < 64; round++) {
        uint8_t req[6] = {0x51, 0x83, 0xf3, (uint8_t)((offset >> 8) & 0xff),
                          (uint8_t)(offset & 0xff), 0};
        req[5] = ddc_checksum(0x6e, req, 5);
        uint8_t resp[48] = {};
        int n = xfer(fd_, req, sizeof(req), resp, sizeof(resp), 60000);
        if (n < 6 || resp[0] != 0x6e || resp[2] != 0xe3) {
            return out.empty() ? (n < 0 ? n : (int)DdcErr::Protocol) : (int)out.size();
        }
        uint8_t len = resp[1] & 0x7f;
        if (len < 3) {
            break;
        }
        size_t data_len = (size_t)len - 3;
        if (5 + data_len > (size_t)n) {
            data_len = (size_t)n > 5 ? (size_t)n - 5 : 0;
        }
        size_t copy = 0;
        for (; copy < data_len; copy++) {
            if (resp[5 + copy] == '\0') {
                break;
            }
        }
        out.append(reinterpret_cast<char*>(&resp[5]), copy);
        if (copy < data_len || data_len == 0) {
            break;
        }
        offset = (uint16_t)(offset + copy);
        if (copy == 0) {
            break;
        }
    }
    return (int)out.size();
}

DdcErr DdcBus::read_edid(uint8_t edid[128]) {
    if (fd_ < 0) {
        return DdcErr::Io;
    }
    if (ioctl(fd_, I2C_SLAVE_FORCE, DDC_EDID_ADDR) < 0 && ioctl(fd_, I2C_SLAVE, DDC_EDID_ADDR) < 0) {
        return DdcErr::Io;
    }
    uint8_t zero = 0;
    if (write(fd_, &zero, 1) != 1) {
        return DdcErr::Io;
    }
    usleep(10000);
    if (read(fd_, edid, 128) != 128) {
        return DdcErr::Io;
    }
    if (edid[0] != 0x00 || edid[1] != 0xff || edid[7] != 0x00) {
        return DdcErr::Protocol;
    }
    return DdcErr::Ok;
}

const char* DdcBus::strerror(DdcErr err) {
    switch (err) {
    case DdcErr::Ok:
        return "ok";
    case DdcErr::Io:
        return "I2C I/O error";
    case DdcErr::Open:
        return "cannot open I2C device";
    case DdcErr::Checksum:
        return "DDC checksum error";
    case DdcErr::Protocol:
        return "DDC protocol error";
    case DdcErr::Unsupported:
        return "feature unsupported";
    case DdcErr::Timeout:
        return "DDC timeout";
    }
    return "unknown DDC error";
}
