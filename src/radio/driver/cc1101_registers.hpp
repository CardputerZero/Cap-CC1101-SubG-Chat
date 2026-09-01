#pragma once

#include <cstdint>

namespace cc1101_driver {

inline constexpr uint8_t kMcsm1RxOffMask     = 0x0C;
inline constexpr uint8_t kMcsm1TxOffMask     = 0x03;
inline constexpr uint8_t kMcsm1RxOffStayRx   = 0x0C;
inline constexpr uint8_t kMcsm1TxOffStayIdle = 0x00;

inline constexpr uint8_t kRxBytesOverflow  = 0x80;
inline constexpr uint8_t kRxBytesCountMask = 0x7F;

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
