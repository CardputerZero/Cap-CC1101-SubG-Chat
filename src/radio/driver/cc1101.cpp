#include "cc1101.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
constexpr uint8_t CMD_READ          = 0x80;
constexpr uint8_t CMD_BURST         = 0x40;
constexpr uint8_t CMD_ACCESS_STATUS = 0x40;
constexpr uint8_t CMD_RESET         = 0x30;
constexpr uint8_t CMD_FSTXON        = 0x31;
constexpr uint8_t CMD_RX            = 0x34;
constexpr uint8_t CMD_TX            = 0x35;
constexpr uint8_t CMD_IDLE          = 0x36;
constexpr uint8_t CMD_FLUSH_RX      = 0x3A;
constexpr uint8_t CMD_FLUSH_TX      = 0x3B;

constexpr uint8_t REG_IOCFG2    = 0x00;
constexpr uint8_t REG_IOCFG0    = 0x02;
constexpr uint8_t REG_FIFOTHR   = 0x03;
constexpr uint8_t REG_SYNC1     = 0x04;
constexpr uint8_t REG_SYNC0     = 0x05;
constexpr uint8_t REG_PKTLEN    = 0x06;
constexpr uint8_t REG_PKTCTRL1  = 0x07;
constexpr uint8_t REG_PKTCTRL0  = 0x08;
constexpr uint8_t REG_ADDR      = 0x09;
constexpr uint8_t REG_CHANNR    = 0x0A;
constexpr uint8_t REG_FSCTRL1   = 0x0B;
constexpr uint8_t REG_FSCTRL0   = 0x0C;
constexpr uint8_t REG_FREQ2     = 0x0D;
constexpr uint8_t REG_FREQ1     = 0x0E;
constexpr uint8_t REG_FREQ0     = 0x0F;
constexpr uint8_t REG_MDMCFG4   = 0x10;
constexpr uint8_t REG_MDMCFG3   = 0x11;
constexpr uint8_t REG_MDMCFG2   = 0x12;
constexpr uint8_t REG_MDMCFG1   = 0x13;
constexpr uint8_t REG_MDMCFG0   = 0x14;
constexpr uint8_t REG_DEVIATN   = 0x15;
constexpr uint8_t REG_MCSM2     = 0x16;
constexpr uint8_t REG_MCSM1     = 0x17;
constexpr uint8_t REG_MCSM0     = 0x18;
constexpr uint8_t REG_FOCCFG    = 0x19;
constexpr uint8_t REG_BSCFG     = 0x1A;
constexpr uint8_t REG_AGCCTRL2  = 0x1B;
constexpr uint8_t REG_AGCCTRL1  = 0x1C;
constexpr uint8_t REG_AGCCTRL0  = 0x1D;
constexpr uint8_t REG_WOREVT1   = 0x1E;
constexpr uint8_t REG_WOREVT0   = 0x1F;
constexpr uint8_t REG_WORCTRL   = 0x20;
constexpr uint8_t REG_FREND1    = 0x21;
constexpr uint8_t REG_FREND0    = 0x22;
constexpr uint8_t REG_FSCAL3    = 0x23;
constexpr uint8_t REG_FSCAL2    = 0x24;
constexpr uint8_t REG_FSCAL1    = 0x25;
constexpr uint8_t REG_FSCAL0    = 0x26;
constexpr uint8_t REG_RCCTRL1   = 0x27;
constexpr uint8_t REG_RCCTRL0   = 0x28;
constexpr uint8_t REG_FSTEST    = 0x29;
constexpr uint8_t REG_PTEST     = 0x2A;
constexpr uint8_t REG_AGCTEST   = 0x2B;
constexpr uint8_t REG_TEST2     = 0x2C;
constexpr uint8_t REG_TEST1     = 0x2D;
constexpr uint8_t REG_TEST0     = 0x2E;
constexpr uint8_t REG_PARTNUM   = 0x30;
constexpr uint8_t REG_VERSION   = 0x31;
constexpr uint8_t REG_MARCSTATE = 0x35;
constexpr uint8_t REG_TXBYTES   = 0x3A;
constexpr uint8_t REG_RXBYTES   = 0x3B;
constexpr uint8_t REG_PATABLE   = 0x3E;
constexpr uint8_t REG_FIFO      = 0x3F;

constexpr uint8_t GDOX_RX_FIFO_FULL_OR_PKT_END        = 0x01;
constexpr uint8_t GDOX_SYNC_WORD_SENT_OR_PKT_RECEIVED = 0x06;
constexpr uint8_t GDOX_HIGH_Z                         = 0x2E;
constexpr uint8_t FIFO_THR_TX_1_RX_64                 = 0x0F;
constexpr uint8_t LENGTH_VARIABLE                     = 0x01;
constexpr uint8_t CRC_ON                              = 0x04;
constexpr uint8_t CRC_OK                              = 0x80;
constexpr uint8_t APPEND_STATUS_ON                    = 0x04;
constexpr uint8_t ADR_CHK_NONE                        = 0x00;
constexpr uint8_t MOD_FORMAT_2_FSK                    = 0x00;
constexpr uint8_t MANCHESTER_OFF                      = 0x00;
constexpr uint8_t WHITE_DATA_OFF                      = 0x00;
constexpr uint8_t PKT_FORMAT_NORMAL                   = 0x00;
constexpr uint8_t SYNC_MODE_16_16                     = 0x02;
constexpr uint8_t FS_AUTOCAL_IDLE_TO_RXTX             = 0x10;
constexpr uint8_t PIN_CTRL_OFF                        = 0x00;
constexpr uint8_t RXOFF_IDLE                          = 0x00;
constexpr uint8_t TXOFF_IDLE                          = 0x00;
constexpr size_t FIFO_SIZE                            = 64;
constexpr size_t MAX_PACKET                           = 255;
constexpr float XOSC_MHZ                              = 26.0f;

bool isStatusRegister(uint8_t reg)
{
    return reg > REG_TEST0 && reg < REG_PATABLE;
}

uint8_t statusAddress(uint8_t reg)
{
    return isStatusRegister(reg) ? static_cast<uint8_t>(reg | CMD_ACCESS_STATUS) : reg;
}

bool isKnownVersion(uint8_t ver)
{
    return ver == 0x04 || ver == 0x14 || ver == 0x20 || ver == 0x3F || ver == 0x94 || ver == 0xBF || ver == 0xFC;
}

std::string hexByte(uint8_t value)
{
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result      = "0x00";
    result[2]               = digits[(value >> 4) & 0x0F];
    result[3]               = digits[value & 0x0F];
    return result;
}

uint8_t preambleValue(uint8_t bytes)
{
    switch (bytes) {
        case 16:
            return 0x00;
        case 24:
            return 0x10;
        case 32:
            return 0x20;
        case 48:
            return 0x30;
        case 64:
            return 0x40;
        case 96:
            return 0x50;
        case 128:
            return 0x60;
        case 192:
            return 0x70;
        default:
            throw std::runtime_error("unsupported preamble length; use 16/24/32/48/64/96/128/192 bits");
    }
}
}  // namespace

CC1101Radio::CC1101Radio(SpiDevice& spi, const BoardPins& pins)
    : spi_(spi),
      pins_(pins),
      cs_(pins.cs_gpio),
      gdo0_(pins.gdo0_gpio),
      rf_sw0_(pins.rf_sw0_gpio),
      reset_(pins.reset_gpio)
{
}

void CC1101Radio::throwIfCancelled(const std::atomic_bool* cancel)
{
    if (cancel && cancel->load(std::memory_order_acquire)) throw CC1101Cancelled{};
}

void CC1101Radio::interruptibleSleep(int milliseconds, const std::atomic_bool* cancel)
{
    int remaining = std::max(0, milliseconds);
    while (remaining > 0) {
        throwIfCancelled(cancel);
        int slice = std::min(remaining, 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(slice));
        remaining -= slice;
    }
    throwIfCancelled(cancel);
}

void CC1101Radio::csLow()
{
    cs_.setValue(false);
}
void CC1101Radio::csHigh()
{
    cs_.setValue(true);
}

uint8_t CC1101Radio::command(uint8_t cmd)
{
    csLow();
    uint8_t status = spi_.transfer(cmd);
    csHigh();
    return status;
}

uint8_t CC1101Radio::readRegRaw(uint8_t reg)
{
    uint8_t tx[2] = {static_cast<uint8_t>(statusAddress(reg) | CMD_READ), 0x00};
    uint8_t rx[2] = {};
    csLow();
    spi_.transfer(tx, rx, sizeof(tx));
    csHigh();
    return rx[1];
}

void CC1101Radio::writeRegRaw(uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = {statusAddress(reg), value};
    csLow();
    spi_.writeBytes(tx, sizeof(tx));
    csHigh();
    if (reg < sizeof(reg_shadow_)) {
        reg_shadow_[reg]       = value;
        reg_shadow_valid_[reg] = true;
    }
}

void CC1101Radio::readBurst(uint8_t reg, uint8_t* data, size_t len)
{
    std::vector<uint8_t> tx(len + 1, 0x00), rx(len + 1, 0x00);
    tx[0] = static_cast<uint8_t>(statusAddress(reg) | CMD_READ | CMD_BURST);
    csLow();
    spi_.transfer(tx.data(), rx.data(), tx.size());
    csHigh();
    std::copy(rx.begin() + 1, rx.end(), data);
}

void CC1101Radio::readFifoBytes(uint8_t* data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        data[i] = readRegRaw(REG_FIFO);
    }
}

void CC1101Radio::writeBurst(uint8_t reg, const uint8_t* data, size_t len)
{
    std::vector<uint8_t> tx(len + 1, 0x00);
    tx[0] = static_cast<uint8_t>(statusAddress(reg) | CMD_BURST);
    std::copy(data, data + len, tx.begin() + 1);
    csLow();
    spi_.writeBytes(tx.data(), tx.size());
    csHigh();
}

uint8_t CC1101Radio::readRegister(uint8_t reg)
{
    return readRegRaw(reg);
}
void CC1101Radio::writeRegister(uint8_t reg, uint8_t value)
{
    writeRegRaw(reg, value);
}

uint8_t CC1101Radio::getRegBits(uint8_t reg, uint8_t msb, uint8_t lsb)
{
    uint8_t value = readRegRaw(reg);
    uint8_t mask  = static_cast<uint8_t>(((1u << (msb - lsb + 1)) - 1u) << lsb);
    return static_cast<uint8_t>((value & mask) >> lsb);
}

void CC1101Radio::setRegBits(uint8_t reg, uint8_t value, uint8_t msb, uint8_t lsb)
{
    uint8_t cur =
        (relaxed_status_ && reg < sizeof(reg_shadow_) && reg_shadow_valid_[reg]) ? reg_shadow_[reg] : readRegRaw(reg);
    if (relaxed_status_ && reg < sizeof(reg_shadow_) && !reg_shadow_valid_[reg]) cur = 0;
    uint8_t mask = static_cast<uint8_t>(((1u << (msb - lsb + 1)) - 1u) << lsb);
    cur          = static_cast<uint8_t>((cur & ~mask) | (value & mask));
    writeRegRaw(reg, cur);
}

void CC1101Radio::resetChip(const std::atomic_bool* cancel)
{
    throwIfCancelled(cancel);
    if (reset_.valid()) {
        reset_.exportLine();
        reset_.setDirection(GpioLine::Direction::Out);
        reset_.setValue(false);
        interruptibleSleep(2, cancel);
        reset_.setValue(true);
        interruptibleSleep(5, cancel);
    }
    if (!skip_sres_) {
        command(CMD_RESET);
        interruptibleSleep(150, cancel);
    }
}

void CC1101Radio::begin(const RadioConfig& cfg, const std::atomic_bool* cancel)
{
    throwIfCancelled(cancel);
    cfg_ = cfg;
    spdlog::info("CC1101 driver: setting up control lines (CS={}, GDO0={}, RF_SW0={}, reset={})",
                 pins_.cs_gpio < 0 ? "kernel" : std::to_string(pins_.cs_gpio),
                 pins_.gdo0_gpio < 0 ? "disabled" : std::to_string(pins_.gdo0_gpio),
                 pins_.rf_sw0_gpio < 0 ? "disabled" : std::to_string(pins_.rf_sw0_gpio),
                 pins_.reset_gpio < 0 ? "SRES" : std::to_string(pins_.reset_gpio));
    cs_.exportLine();
    cs_.setDirection(GpioLine::Direction::Out);
    csHigh();
    if (rf_sw0_.valid()) {
        spdlog::debug("CC1101 driver: requesting RF_SW0 GPIO{} as output", pins_.rf_sw0_gpio);
        rf_sw0_.exportLine();
        rf_sw0_.setDirection(GpioLine::Direction::Out);
    }
    if (gdo0_.valid()) {
        spdlog::debug("CC1101 driver: requesting GDO0 GPIO{} as rising-edge input", pins_.gdo0_gpio);
        gdo0_.exportLine();
        gdo0_.setDirection(GpioLine::Direction::In);
        gdo0_.setEdge("rising");
    }

    spdlog::info("CC1101 driver: resetting and probing transceiver");
    resetChip(cancel);
    uint8_t part = 0;
    uint8_t ver  = 0;
    for (int i = 0; i < 10; ++i) {
        throwIfCancelled(cancel);
        part = readRegRaw(REG_PARTNUM);
        ver  = chipVersion();
        spdlog::debug("CC1101 driver: probe {}/10 returned PARTNUM=0x{:02X}, VERSION=0x{:02X}", i + 1,
                      static_cast<unsigned>(part), static_cast<unsigned>(ver));
        if (isKnownVersion(ver)) break;
        interruptibleSleep(20, cancel);
    }
    if (!isKnownVersion(ver)) {
        throw std::runtime_error("CC1101 probe failed after 10 attempts: PARTNUM=" + hexByte(part) +
                                 ", VERSION=" + hexByte(ver));
    }
    relaxed_status_ = (ver == 0x3F || ver == 0x94 || ver == 0xBF || ver == 0xFC);
    spdlog::info("CC1101 driver: detected PARTNUM=0x{:02X}, VERSION=0x{:02X}, relaxed_status={}",
                 static_cast<unsigned>(part), static_cast<unsigned>(ver), relaxed_status_);

    spdlog::debug("CC1101 driver: programming modem, packet, calibration, and antenna registers");
    standby();
    setRegBits(REG_MCSM0, FS_AUTOCAL_IDLE_TO_RXTX | PIN_CTRL_OFF, 5, 1);
    writeRegRaw(REG_IOCFG0, GDOX_HIGH_Z);
    writeRegRaw(REG_IOCFG2, GDOX_HIGH_Z);
    configurePacketMode();
    setFrequency(cfg_.freq_mhz);
    setBitRate(cfg_.bit_rate_kbps);
    setRxBandwidth(cfg_.rx_bw_khz);
    setFrequencyDeviation(cfg_.freq_dev_khz);
    setOutputPower(cfg_.power_dbm);
    setRegBits(REG_PKTCTRL0, LENGTH_VARIABLE, 1, 0);
    writeRegRaw(REG_PKTLEN, 0xFF);
    setPreamble(cfg_.preamble_bytes);
    setRegBits(REG_MDMCFG2, MOD_FORMAT_2_FSK, 6, 4);
    setRegBits(REG_MDMCFG2, MANCHESTER_OFF, 3, 3);
    setRegBits(REG_PKTCTRL0, WHITE_DATA_OFF, 6, 6);
    setSyncWord();
    applyRadioLib868LowConfig();
    flushRx();
    flushTx();
    selectAntenna();
    throwIfCancelled(cancel);
    spdlog::info("CC1101 driver: profile configured (2-FSK, variable length, CRC, sync=0x12AD)");
}

void CC1101Radio::standby(int timeout_ms)
{
    command(CMD_IDLE);
    if (relaxed_status_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return;
    }
    auto start = std::chrono::steady_clock::now();
    while (getRegBits(REG_MARCSTATE, 4, 0) != 0x01) {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() >
            timeout_ms) {
            throw std::runtime_error("CC1101 standby timeout");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void CC1101Radio::configurePacketMode()
{
    setRegBits(REG_PKTCTRL1, APPEND_STATUS_ON | ADR_CHK_NONE, 3, 0);
    setRegBits(REG_PKTCTRL0, WHITE_DATA_OFF | PKT_FORMAT_NORMAL, 6, 4);
    setRegBits(REG_PKTCTRL0, CRC_ON | LENGTH_VARIABLE, 2, 0);
    writeRegRaw(REG_ADDR, 0x00);
    setRegBits(REG_MCSM1, RXOFF_IDLE | TXOFF_IDLE, 3, 0);
}

void CC1101Radio::setFrequency(float freq_mhz)
{
    if (!(((freq_mhz >= 300.0f) && (freq_mhz <= 348.0f)) || ((freq_mhz >= 387.0f) && (freq_mhz <= 464.0f)) ||
          ((freq_mhz >= 779.0f) && (freq_mhz <= 928.0f)))) {
        throw std::runtime_error("invalid CC1101 frequency");
    }
    standby();
    uint32_t frf = static_cast<uint32_t>((freq_mhz * 65536.0f) / XOSC_MHZ);
    writeRegRaw(REG_FREQ2, static_cast<uint8_t>((frf >> 16) & 0xFF));
    writeRegRaw(REG_FREQ1, static_cast<uint8_t>((frf >> 8) & 0xFF));
    writeRegRaw(REG_FREQ0, static_cast<uint8_t>(frf & 0xFF));
}

void CC1101Radio::setBitRate(float br_kbps)
{
    if (br_kbps < 0.025f || br_kbps > 600.0f) throw std::runtime_error("invalid bit rate");
    standby();
    uint8_t e = 0, m = 0;
    getExpMant(br_kbps * 1000.0f, 256, 28, 14, e, m);
    setRegBits(REG_MDMCFG4, e, 3, 0);
    writeRegRaw(REG_MDMCFG3, m);
}

void CC1101Radio::setRxBandwidth(float rx_bw_khz)
{
    if (rx_bw_khz < 58.0f || rx_bw_khz > 812.0f) throw std::runtime_error("invalid RX bandwidth");
    standby();
    for (int8_t e = 3; e >= 0; --e) {
        for (int8_t m = 3; m >= 0; --m) {
            float point = (XOSC_MHZ * 1000000.0f) / (8.0f * (m + 4) * (1u << e));
            if (std::fabs(rx_bw_khz * 1000.0f - point) <= 1000.0f) {
                setRegBits(REG_MDMCFG4, static_cast<uint8_t>((e << 6) | (m << 4)), 7, 4);
                return;
            }
        }
    }
    throw std::runtime_error(
        "unsupported RX bandwidth; typical values: 58,68,81,102,116,135,162,203,232,270,325,406,464,541,650,812 kHz");
}

void CC1101Radio::setFrequencyDeviation(float dev_khz)
{
    if (dev_khz < 1.587f || dev_khz > 380.8f) throw std::runtime_error("invalid frequency deviation");
    standby();
    uint8_t e = 0, m = 0;
    getExpMant(dev_khz * 1000.0f, 8, 17, 7, e, m);
    setRegBits(REG_DEVIATN, static_cast<uint8_t>(e << 4), 6, 4);
    setRegBits(REG_DEVIATN, m, 2, 0);
}

void CC1101Radio::setOutputPower(int8_t power_dbm)
{
    writeRegRaw(REG_PATABLE, paTableValue(cfg_.freq_mhz, power_dbm));
}

void CC1101Radio::setPreamble(uint8_t preamble_bytes)
{
    uint8_t pqt = std::min<uint8_t>(7, preamble_bytes / 4);
    setRegBits(REG_PKTCTRL1, static_cast<uint8_t>(pqt << 5), 7, 5);
    setRegBits(REG_MDMCFG1, preambleValue(preamble_bytes), 6, 4);
}

void CC1101Radio::setSyncWord(uint8_t msb, uint8_t lsb)
{
    setRegBits(REG_MDMCFG2, SYNC_MODE_16_16, 2, 0);
    writeRegRaw(REG_SYNC1, msb);
    writeRegRaw(REG_SYNC0, lsb);
}

void CC1101Radio::applyRadioLib868LowConfig()
{
    if (std::fabs(cfg_.freq_mhz - 868.0f) > 0.5f || std::fabs(cfg_.bit_rate_kbps - 2.4f) > 0.2f ||
        std::fabs(cfg_.rx_bw_khz - 58.0f) > 2.0f) {
        return;
    }

    // Mirror RadioLib 7.7 CC1101 register dump from the original Cap_CC1101 firmware.
    writeRegRaw(REG_IOCFG2, 0x6F);  // RF_SW1 high on GDO2 for 868 MHz.
    writeRegRaw(0x01, 0x2E);        // IOCFG1 high-Z.
    writeRegRaw(REG_IOCFG0, 0x2E);  // GDO0 high-Z until startReceive().
    writeRegRaw(REG_FIFOTHR, 0x07);
    writeRegRaw(REG_SYNC1, 0x12);
    writeRegRaw(REG_SYNC0, 0xAD);
    writeRegRaw(REG_PKTLEN, 0xFF);
    writeRegRaw(REG_PKTCTRL1, 0x64);
    writeRegRaw(REG_PKTCTRL0, 0x05);
    writeRegRaw(REG_ADDR, 0x00);
    writeRegRaw(REG_CHANNR, 0x00);
    writeRegRaw(REG_FSCTRL1, 0x0F);
    writeRegRaw(REG_FSCTRL0, 0x00);
    writeRegRaw(REG_FREQ2, 0x21);
    writeRegRaw(REG_FREQ1, 0x62);
    writeRegRaw(REG_FREQ0, 0x76);
    writeRegRaw(REG_MDMCFG4, 0xF6);
    writeRegRaw(REG_MDMCFG3, 0x83);
    writeRegRaw(REG_MDMCFG2, 0x02);
    writeRegRaw(REG_MDMCFG1, 0x02);
    writeRegRaw(REG_MDMCFG0, 0xF8);
    writeRegRaw(REG_DEVIATN, 0x40);
    writeRegRaw(REG_MCSM2, 0x07);
    writeRegRaw(REG_MCSM1, 0x30);
    writeRegRaw(REG_MCSM0, 0x14);
    writeRegRaw(REG_FOCCFG, 0x76);
    writeRegRaw(REG_BSCFG, 0x6C);
    writeRegRaw(REG_AGCCTRL2, 0x03);
    writeRegRaw(REG_AGCCTRL1, 0x40);
    writeRegRaw(REG_AGCCTRL0, 0x91);
    writeRegRaw(REG_WOREVT1, 0x87);
    writeRegRaw(REG_WOREVT0, 0x6B);
    writeRegRaw(REG_WORCTRL, 0xF8);
    writeRegRaw(REG_FREND1, 0x56);
    writeRegRaw(REG_FREND0, 0x10);
    writeRegRaw(REG_FSCAL3, 0xA9);
    writeRegRaw(REG_FSCAL2, 0x0A);
    writeRegRaw(REG_FSCAL1, 0x20);
    writeRegRaw(REG_FSCAL0, 0x0D);
    writeRegRaw(REG_RCCTRL1, 0x41);
    writeRegRaw(REG_RCCTRL0, 0x00);
    writeRegRaw(REG_FSTEST, 0x59);
    writeRegRaw(REG_PTEST, 0x7F);
    writeRegRaw(REG_AGCTEST, 0x3F);
    writeRegRaw(REG_TEST2, 0x88);
    writeRegRaw(REG_TEST1, 0x31);
    writeRegRaw(REG_TEST0, 0x0B);
    writeRegRaw(REG_PATABLE, 0xC2);
}

void CC1101Radio::selectAntenna()
{
    bool sw0     = true;
    uint8_t gdo2 = 0x6F;
    if (cfg_.freq_mhz < 374.0f) {
        sw0  = false;
        gdo2 = 0x6F;
    } else if (cfg_.freq_mhz < 650.5f) {
        sw0  = true;
        gdo2 = 0x2F;
    } else {
        sw0  = true;
        gdo2 = 0x6F;
    }
    if (rf_sw0_.valid()) rf_sw0_.setValue(sw0);
    writeRegRaw(REG_IOCFG2, gdo2);
}

void CC1101Radio::flushRx()
{
    command(CMD_IDLE);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    command(CMD_FLUSH_RX);
    command(CMD_FLUSH_RX);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
}
void CC1101Radio::flushTx()
{
    command(CMD_FLUSH_TX);
}

void CC1101Radio::startReceive()
{
    standby();
    flushRx();
    writeRegRaw(REG_IOCFG0, GDOX_RX_FIFO_FULL_OR_PKT_END);
    setRegBits(REG_FIFOTHR, FIFO_THR_TX_1_RX_64, 3, 0);
    selectAntenna();
    command(CMD_RX);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

void CC1101Radio::idle()
{
    standby();
    flushRx();
    flushTx();
    selectAntenna();
}

bool CC1101Radio::waitForRxBytesAtLeast(uint8_t min_bytes, int timeout_ms, const std::atomic_bool* cancel)
{
    auto start = std::chrono::steady_clock::now();
    while (true) {
        throwIfCancelled(cancel);
        if ((readRegRaw(REG_RXBYTES) & 0x7F) >= min_bytes) return true;
        if (timeout_ms >= 0 &&
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() >
                timeout_ms) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

size_t CC1101Radio::readPacketLength()
{
    return readRegRaw(REG_FIFO);
}

bool CC1101Radio::receive(RxPacket& packet, int timeout_ms, const std::atomic_bool* cancel)
{
    throwIfCancelled(cancel);
    auto start = std::chrono::steady_clock::now();
    while (true) {
        throwIfCancelled(cancel);
        bool ready  = false;
        int wait_ms = timeout_ms;
        if (timeout_ms >= 0) {
            int elapsed = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                    .count());
            wait_ms = timeout_ms - elapsed;
            if (wait_ms <= 0) return false;
        }

        while (true) {
            throwIfCancelled(cancel);
            if ((readRegRaw(REG_RXBYTES) & 0x7F) > 0) {
                ready = true;
                break;
            }
            if (wait_ms >= 0 &&
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                        .count() > timeout_ms) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!ready) return false;

        size_t len = readPacketLength();
        if (len == 0 || len > FIFO_SIZE - 3) {
            flushRx();
            startReceive();
            continue;
        }

        if (!waitForRxBytesAtLeast(static_cast<uint8_t>(len + 2), 1000, cancel)) {
            flushRx();
            startReceive();
            continue;
        }
        packet.data.assign(len, 0);
        readFifoBytes(packet.data.data(), len);
        raw_rssi_       = readRegRaw(REG_FIFO);
        uint8_t lqi_crc = readRegRaw(REG_FIFO);

        raw_lqi_        = lqi_crc & 0x7F;
        packet.rssi_dbm = rssiFromRaw(raw_rssi_);
        packet.lqi      = raw_lqi_;
        packet.crc_ok   = (lqi_crc & CRC_OK) != 0;
        standby();
        flushRx();
        startReceive();
        return true;
    }
}

std::vector<uint8_t> CC1101Radio::rawReceive(size_t max_len, int timeout_ms)
{
    standby();
    flushRx();
    startReceive();
    auto start      = std::chrono::steady_clock::now();
    uint8_t rxbytes = 0;
    while (true) {
        rxbytes = static_cast<uint8_t>(readRegRaw(REG_RXBYTES) & 0x7F);
        if (rxbytes > 0) break;
        if (timeout_ms >= 0 &&
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() >
                timeout_ms) {
            return {};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    uint8_t len = readRegRaw(REG_FIFO);
    std::vector<uint8_t> data;
    data.push_back(len);
    if (len > 0 && len <= FIFO_SIZE - 3) {
        (void)waitForRxBytesAtLeast(static_cast<uint8_t>(len + 2), 1000);
        rxbytes  = static_cast<uint8_t>((readRegRaw(REG_RXBYTES) & 0x7F) + 1);
        size_t n = std::min<size_t>(rxbytes, max_len);
        data.reserve(n);
        for (size_t i = 1; i < n; ++i) data.push_back(readRegRaw(REG_FIFO));
    }
    std::cerr << "raw RXBYTES=" << static_cast<int>(rxbytes) << " MARCSTATE=0x" << std::hex
              << static_cast<int>(readRegRaw(REG_MARCSTATE)) << " PKTSTATUS=0x" << static_cast<int>(readRegRaw(0x38))
              << std::dec << "\n";
    standby();
    flushRx();
    selectAntenna();
    return data;
}

bool CC1101Radio::receiveFixed(RxPacket& packet, size_t len, int timeout_ms)
{
    if (len == 0 || len > MAX_PACKET - 2) throw std::runtime_error("invalid fixed receive length");
    standby();
    flushRx();
    writeRegRaw(REG_PKTLEN, static_cast<uint8_t>(len));
    setRegBits(REG_PKTCTRL0, 0x00, 1, 0);  // fixed packet length
    startReceive();

    auto start = std::chrono::steady_clock::now();
    while (true) {
        bool ready  = false;
        int elapsed = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
        int wait_ms = timeout_ms >= 0 ? timeout_ms - elapsed : timeout_ms;
        if (timeout_ms >= 0 && wait_ms <= 0) return false;
        while (
            timeout_ms < 0 ||
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() <=
                timeout_ms) {
            if ((readRegRaw(REG_RXBYTES) & 0x7F) >= len + 2) {
                ready = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!ready) return false;
        (void)wait_ms;
        packet.data.assign(len, 0);
        readFifoBytes(packet.data.data(), len);
        raw_rssi_       = readRegRaw(REG_FIFO);
        uint8_t lqi_crc = readRegRaw(REG_FIFO);
        raw_lqi_        = lqi_crc & 0x7F;
        packet.rssi_dbm = rssiFromRaw(raw_rssi_);
        packet.lqi      = raw_lqi_;
        packet.crc_ok   = (lqi_crc & CRC_OK) != 0;
        standby();
        flushRx();
        configurePacketMode();
        applyRadioLib868LowConfig();
        selectAntenna();
        return true;
    }
}

void CC1101Radio::transmit(const uint8_t* data, size_t len, int timeout_ms, const std::atomic_bool* cancel)
{
    throwIfCancelled(cancel);
    if (len > MAX_PACKET - 1) throw std::runtime_error("packet too long for variable-length CC1101 packet");
    standby();
    flushTx();

    if (relaxed_status_) {
        if (len > FIFO_SIZE - 1) {
            throw std::runtime_error("relaxed TX path supports payloads up to 63 bytes");
        }
        uint8_t plen = static_cast<uint8_t>(len);
        writeRegRaw(REG_FIFO, plen);
        if (len > 0) writeBurst(REG_FIFO, data, len);
        selectAntenna();
        command(CMD_TX);
        sleepForPacket(len, 150, cancel);
        standby();
        flushTx();
        selectAntenna();
        return;
    }

    command(CMD_FSTXON);
    auto start_us = std::chrono::steady_clock::now();
    while (getRegBits(REG_MARCSTATE, 4, 0) != 0x12) {
        throwIfCancelled(cancel);
        if (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_us).count() >
            1600) {
            standby();
            throw std::runtime_error("TX oscillator ready timeout");
        }
    }
    if (len <= FIFO_SIZE) setRegBits(REG_IOCFG2, GDOX_SYNC_WORD_SENT_OR_PKT_RECEIVED, 5, 0);
    uint8_t plen = static_cast<uint8_t>(len);
    writeRegRaw(REG_FIFO, plen);
    size_t initial = std::min(len, FIFO_SIZE - 1);
    if (initial > 0) writeBurst(REG_FIFO, data, initial);
    size_t sent = initial;
    selectAntenna();
    command(CMD_TX);

    auto start = std::chrono::steady_clock::now();
    while (sent < len) {
        throwIfCancelled(cancel);
        uint8_t fifo_bytes = getRegBits(REG_TXBYTES, 6, 0);
        if (fifo_bytes < FIFO_SIZE) {
            size_t n = std::min(FIFO_SIZE - fifo_bytes, len - sent);
            writeBurst(REG_FIFO, data + sent, n);
            sent += n;
        }
        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() >
            timeout_ms) {
            standby();
            flushTx();
            throw std::runtime_error("TX FIFO feed timeout");
        }
    }

    // Match the verified ADV firmware behavior: wait for the packet's on-air
    // time instead of depending on MARCSTATE, which is flaky on this Linux SPI path.
    sleepForPacket(len, 150, cancel);
    standby();
    flushTx();
    selectAntenna();
}

void CC1101Radio::transmitString(const std::string& text, int timeout_ms, const std::atomic_bool* cancel)
{
    transmit(reinterpret_cast<const uint8_t*>(text.data()), text.size(), timeout_ms, cancel);
}

uint8_t CC1101Radio::chipVersion()
{
    return readRegRaw(REG_VERSION);
}
uint8_t CC1101Radio::marcState()
{
    return getRegBits(REG_MARCSTATE, 4, 0);
}

void CC1101Radio::sleepForPacket(size_t payload_len, int min_ms, const std::atomic_bool* cancel) const
{
    // Same conservative margin as the ADV serial firmware: payload plus packet
    // overhead, converted from kbps to milliseconds, with a floor for settling.
    int ms = 150 + static_cast<int>(((payload_len + 24) * 8) / cfg_.bit_rate_kbps);
    if (ms < min_ms) ms = min_ms;
    interruptibleSleep(ms, cancel);
}

void CC1101Radio::dumpRegisters() const
{
    auto* self                 = const_cast<CC1101Radio*>(this);
    static const char* names[] = {
        "IOCFG2",  "IOCFG1",  "IOCFG0",  "FIFOTHR", "SYNC1",  "SYNC0",  "PKTLEN",  "PKTCTRL1", "PKTCTRL0", "ADDR",
        "CHANNR",  "FSCTRL1", "FSCTRL0", "FREQ2",   "FREQ1",  "FREQ0",  "MDMCFG4", "MDMCFG3",  "MDMCFG2",  "MDMCFG1",
        "MDMCFG0", "DEVIATN", "MCSM2",   "MCSM1",   "MCSM0",  "FOCCFG", "BSCFG",   "AGCCTRL2", "AGCCTRL1", "AGCCTRL0",
        "WOREVT1", "WOREVT0", "WORCTRL", "FREND1",  "FREND0", "FSCAL3", "FSCAL2",  "FSCAL1",   "FSCAL0",   "RCCTRL1",
        "RCCTRL0", "FSTEST",  "PTEST",   "AGCTEST", "TEST2",  "TEST1",  "TEST0"};
    for (uint8_t reg = 0; reg <= REG_TEST0; ++reg) {
        std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(reg) << " " << std::left
                  << std::setw(10) << names[reg] << " = 0x" << std::right << std::setw(2)
                  << static_cast<int>(self->readRegRaw(reg)) << std::dec << "\n";
    }
    uint8_t status_regs[] = {REG_VERSION, REG_MARCSTATE, REG_TXBYTES, REG_RXBYTES, REG_PATABLE};
    for (uint8_t reg : status_regs) {
        std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(reg)
                  << " STATUS     = 0x" << std::setw(2) << static_cast<int>(self->readRegRaw(reg)) << std::dec << "\n";
    }
}

void CC1101Radio::getExpMant(float target, uint16_t mant_offset, uint8_t div_exp, uint8_t exp_max, uint8_t& exp,
                             uint8_t& mant)
{
    float origin = (mant_offset * XOSC_MHZ * 1000000.0f) / static_cast<float>(1u << div_exp);
    for (int8_t e = static_cast<int8_t>(exp_max); e >= 0; --e) {
        float interval_start = static_cast<float>(1u << e) * origin;
        if (target >= interval_start) {
            exp             = static_cast<uint8_t>(e);
            float step_size = interval_start / static_cast<float>(mant_offset);
            mant            = static_cast<uint8_t>((target - interval_start) / step_size);
            return;
        }
    }
    exp  = 0;
    mant = 0;
}

uint8_t CC1101Radio::paTableValue(float freq_mhz, int8_t power_dbm)
{
    constexpr int8_t allowed[8]   = {-30, -20, -15, -10, 0, 5, 7, 10};
    constexpr uint8_t table[8][4] = {{0x12, 0x12, 0x03, 0x03}, {0x0D, 0x0E, 0x0F, 0x0E}, {0x1C, 0x1D, 0x1E, 0x1E},
                                     {0x34, 0x34, 0x27, 0x27}, {0x51, 0x60, 0x50, 0x8E}, {0x85, 0x84, 0x81, 0xCD},
                                     {0xCB, 0xC8, 0xCB, 0xC7}, {0xC2, 0xC0, 0xC2, 0xC0}};
    int f                         = freq_mhz < 374.0f ? 0 : (freq_mhz < 650.5f ? 1 : (freq_mhz < 891.5f ? 2 : 3));
    for (size_t i = 0; i < 8; ++i) {
        if (allowed[i] == power_dbm) return table[i][f];
    }
    throw std::runtime_error("invalid power; allowed: -30,-20,-15,-10,0,5,7,10 dBm");
}

float CC1101Radio::rssiFromRaw(uint8_t raw)
{
    return raw >= 128 ? ((static_cast<float>(raw) - 256.0f) / 2.0f) - 74.0f : (static_cast<float>(raw) / 2.0f) - 74.0f;
}
