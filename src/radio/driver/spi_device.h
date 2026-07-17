#pragma once

#include <cstdint>
#include <string>
#include <vector>

class SpiDevice {
public:
    SpiDevice() = default;
    ~SpiDevice();

    SpiDevice(const SpiDevice&)            = delete;
    SpiDevice& operator=(const SpiDevice&) = delete;
    SpiDevice(SpiDevice&&)                 = delete;
    SpiDevice& operator=(SpiDevice&&)      = delete;

    void openDevice(const std::string& path, uint32_t speed_hz, uint8_t mode = 0, uint8_t bits = 8,
                    bool no_kernel_cs = true);
    void closeDevice();
    bool isOpen() const
    {
        return fd_ >= 0;
    }

    uint8_t transfer(uint8_t value);
    void transfer(const uint8_t* tx, uint8_t* rx, size_t len);
    void writeBytes(const uint8_t* tx, size_t len);

private:
    int fd_            = -1;
    uint32_t speed_hz_ = 0;
    uint8_t bits_      = 8;
};
