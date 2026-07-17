#include "spi_device.h"

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdexcept>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

SpiDevice::~SpiDevice()
{
    closeDevice();
}

void SpiDevice::openDevice(const std::string& path, uint32_t speed_hz, uint8_t mode, uint8_t bits, bool no_kernel_cs)
{
    closeDevice();
    fd_ = ::open(path.c_str(), O_RDWR);
    if (fd_ < 0) {
        throw std::runtime_error("open SPI failed: " + path + ": " + strerror(errno));
    }

    if (no_kernel_cs) {
        mode |= SPI_NO_CS;
    }
    if (ioctl(fd_, SPI_IOC_WR_MODE, &mode) < 0) {
        throw std::runtime_error("SPI_IOC_WR_MODE failed: " + std::string(strerror(errno)));
    }
    if (ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        throw std::runtime_error("SPI_IOC_WR_BITS_PER_WORD failed: " + std::string(strerror(errno)));
    }
    if (ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz) < 0) {
        throw std::runtime_error("SPI_IOC_WR_MAX_SPEED_HZ failed: " + std::string(strerror(errno)));
    }
    speed_hz_ = speed_hz;
    bits_     = bits;
}

void SpiDevice::closeDevice()
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

uint8_t SpiDevice::transfer(uint8_t value)
{
    uint8_t rx = 0;
    transfer(&value, &rx, 1);
    return rx;
}

void SpiDevice::transfer(const uint8_t* tx, uint8_t* rx, size_t len)
{
    if (fd_ < 0) {
        throw std::runtime_error("SPI device is not open");
    }
    spi_ioc_transfer tr{};
    tr.tx_buf        = reinterpret_cast<unsigned long>(tx);
    tr.rx_buf        = reinterpret_cast<unsigned long>(rx);
    tr.len           = static_cast<uint32_t>(len);
    tr.speed_hz      = speed_hz_;
    tr.bits_per_word = bits_;
    if (ioctl(fd_, SPI_IOC_MESSAGE(1), &tr) < 0) {
        throw std::runtime_error("SPI transfer failed: " + std::string(strerror(errno)));
    }
}

void SpiDevice::writeBytes(const uint8_t* tx, size_t len)
{
    std::vector<uint8_t> rx(len);
    transfer(tx, rx.data(), len);
}
