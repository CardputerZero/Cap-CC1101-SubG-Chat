#pragma once

#include "radio/radio_settings.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace cc1101_driver {

inline constexpr uint8_t kMcsm1RxOffMask     = 0x0C;
inline constexpr uint8_t kMcsm1TxOffMask     = 0x03;
inline constexpr uint8_t kMcsm1RxOffStayRx   = 0x0C;
inline constexpr uint8_t kMcsm1TxOffStayIdle = 0x00;

inline constexpr uint8_t kRxBytesOverflow  = 0x80;
inline constexpr uint8_t kRxBytesCountMask = 0x7F;

struct AntennaSelection {
    bool sw0;
    uint8_t iocfg2;
};

// The Cap uses host GPIO for SW0 and drives SW1 from the CC1101 GDO2 pin.
constexpr AntennaSelection antennaSelection(float frequencyMhz) noexcept
{
    if (frequencyMhz < 374.0F) return {false, 0x6F};
    if (frequencyMhz < 650.5F) return {true, 0x2F};
    return {true, 0x6F};
}

constexpr std::optional<uint8_t> paTableValue(float frequencyMhz, int powerDbm) noexcept
{
    constexpr uint8_t table[][4] = {
        {0x12, 0x12, 0x03, 0x03}, {0x0D, 0x0E, 0x0F, 0x0E}, {0x1C, 0x1D, 0x1E, 0x1E},
        {0x34, 0x34, 0x27, 0x27}, {0x51, 0x60, 0x50, 0x8E}, {0x85, 0x84, 0x81, 0xCD},
        {0xCB, 0xC8, 0xCB, 0xC7}, {0xC2, 0xC0, 0xC2, 0xC0},
    };
    using namespace cc1101_chat::radio;
    const auto index = presetIndex(kPowersDbm, powerDbm);
    if (index >= kPowersDbm.size()) return std::nullopt;
    const int band = frequencyMhz < 374.0F ? 0 : (frequencyMhz < 650.5F ? 1 : (frequencyMhz < 891.5F ? 2 : 3));
    return table[index][band];
}

constexpr uint8_t preambleQualityBits(uint8_t preamble_bits) noexcept
{
    // Like RadioLib, leave four bits of margin for sync detection. PQT is
    // encoded in four-bit steps in PKTCTRL1[7:5].
    const int pqt = std::clamp((preamble_bits - 4) / 4, 0, 7);
    return static_cast<uint8_t>(pqt << 5);
}

constexpr uint8_t persistentReceiveMcsm1(uint8_t current) noexcept
{
    constexpr uint8_t mode_mask = kMcsm1RxOffMask | kMcsm1TxOffMask;
    return static_cast<uint8_t>((current & static_cast<uint8_t>(~mode_mask)) | kMcsm1RxOffStayRx | kMcsm1TxOffStayIdle);
}

constexpr bool rxFifoOverflowed(uint8_t rx_bytes) noexcept
{
    return (rx_bytes & kRxBytesOverflow) != 0;
}

constexpr uint8_t rxFifoByteCount(uint8_t rx_bytes) noexcept
{
    return static_cast<uint8_t>(rx_bytes & kRxBytesCountMask);
}

}  // namespace cc1101_driver
