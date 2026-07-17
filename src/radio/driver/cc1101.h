#pragma once

#include "spi_device.h"
#include "gpio_line.h"

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <exception>
#include <string>
#include <vector>

struct BoardPins {
    int cs_gpio     = 5;
    int gdo0_gpio   = 15;
    int rf_sw0_gpio = 14;
    int reset_gpio  = -1;
};

struct RadioConfig {
    float freq_mhz         = 868.0f;
    float bit_rate_kbps    = 2.4f;
    float freq_dev_khz     = 25.4f;
    float rx_bw_khz        = 58.0f;
    int8_t power_dbm       = 10;
    uint8_t preamble_bytes = 16;
};

struct RxPacket {
    std::vector<uint8_t> data;
    float rssi_dbm = 0.0f;
    uint8_t lqi    = 0;
    bool crc_ok    = false;
};

class CC1101Cancelled final : public std::exception {
public:
    const char* what() const noexcept override
    {
        return "CC1101 operation cancelled";
    }
};

class CC1101Radio {
public:
    CC1101Radio(SpiDevice& spi, const BoardPins& pins);

    CC1101Radio(const CC1101Radio&)            = delete;
    CC1101Radio& operator=(const CC1101Radio&) = delete;
    CC1101Radio(CC1101Radio&&)                 = delete;
    CC1101Radio& operator=(CC1101Radio&&)      = delete;

    void setSkipSres(bool skip)
    {
        skip_sres_ = skip;
    }
    void begin(const RadioConfig& cfg, const std::atomic_bool* cancel = nullptr);
    void startReceive();
    void idle();
    bool receive(RxPacket& packet, int timeout_ms, const std::atomic_bool* cancel = nullptr);
    bool receiveFixed(RxPacket& packet, size_t len, int timeout_ms);
    std::vector<uint8_t> rawReceive(size_t max_len, int timeout_ms);
    void transmit(const uint8_t* data, size_t len, int timeout_ms = 1000, const std::atomic_bool* cancel = nullptr);
    void transmitString(const std::string& text, int timeout_ms = 1000, const std::atomic_bool* cancel = nullptr);

    uint8_t readRegister(uint8_t reg);
    void writeRegister(uint8_t reg, uint8_t value);
    void dumpRegisters() const;
    uint8_t chipVersion();
    uint8_t marcState();
    void selectAntenna();

private:
    SpiDevice& spi_;
    BoardPins pins_;
    GpioLine cs_;
    GpioLine gdo0_;
    GpioLine rf_sw0_;
    GpioLine reset_;
    RadioConfig cfg_;

    uint8_t raw_rssi_            = 0;
    uint8_t raw_lqi_             = 0;
    bool relaxed_status_         = false;
    bool skip_sres_              = false;
    uint8_t reg_shadow_[0x40]    = {};
    bool reg_shadow_valid_[0x40] = {};

    void csLow();
    void csHigh();
    uint8_t command(uint8_t cmd);
    uint8_t readRegRaw(uint8_t reg);
    void writeRegRaw(uint8_t reg, uint8_t value);
    void readBurst(uint8_t reg, uint8_t* data, size_t len);
    void readFifoBytes(uint8_t* data, size_t len);
    void writeBurst(uint8_t reg, const uint8_t* data, size_t len);
    void setRegBits(uint8_t reg, uint8_t value, uint8_t msb, uint8_t lsb);
    uint8_t getRegBits(uint8_t reg, uint8_t msb, uint8_t lsb);

    void resetChip(const std::atomic_bool* cancel);
    void standby(int timeout_ms = 100);
    void configurePacketMode();
    void setFrequency(float freq_mhz);
    void setBitRate(float br_kbps);
    void setRxBandwidth(float rx_bw_khz);
    void setFrequencyDeviation(float dev_khz);
    void setOutputPower(int8_t power_dbm);
    void setPreamble(uint8_t preamble_bytes);
    void setSyncWord(uint8_t msb = 0x12, uint8_t lsb = 0xAD);
    void applyRadioLib868LowConfig();
    void flushRx();
    void flushTx();
    void sleepForPacket(size_t payload_len, int min_ms, const std::atomic_bool* cancel) const;
    bool waitForRxBytesAtLeast(uint8_t min_bytes, int timeout_ms, const std::atomic_bool* cancel = nullptr);
    size_t readPacketLength();

    static void throwIfCancelled(const std::atomic_bool* cancel);
    static void interruptibleSleep(int milliseconds, const std::atomic_bool* cancel);

    static void getExpMant(float target, uint16_t mant_offset, uint8_t div_exp, uint8_t exp_max, uint8_t& exp,
                           uint8_t& mant);
    static uint8_t paTableValue(float freq_mhz, int8_t power_dbm);
    static float rssiFromRaw(uint8_t raw);
};
