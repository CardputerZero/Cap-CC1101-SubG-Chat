#include "radio/driver/cc1101_registers.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main()
{
    using namespace cc1101_driver;

    require(persistentReceiveMcsm1(0x30) == 0x3C,
            "persistent RX must set RXOFF_MODE bits 3:2 without changing CCA mode bits 5:4");
    require(persistentReceiveMcsm1(0xFF) == 0xFC,
            "persistent RX must clear TXOFF_MODE while preserving unrelated MCSM1 bits");
    require(!rxFifoOverflowed(0x3F), "a normal RX byte count was treated as overflow");
    require(rxFifoOverflowed(0x80), "the RX FIFO overflow flag was ignored");
    require(rxFifoByteCount(0xBF) == 0x3F, "the RX FIFO byte count included the overflow flag");
    require(preambleQualityBits(16) == 0x60, "a 16-bit preamble must use a 12-bit quality threshold");
    require(preambleQualityBits(24) == 0xA0, "a 24-bit preamble must use a 20-bit quality threshold");
    require(preambleQualityBits(48) == 0xE0, "the preamble quality threshold must fit the 3-bit field");
    const auto low = antennaSelection(315.0F);
    const auto mid = antennaSelection(433.92F);
    const auto high = antennaSelection(915.0F);
    require(!low.sw0 && low.iocfg2 == 0x6F, "315 MHz must select the low-band matching path");
    require(mid.sw0 && mid.iocfg2 == 0x2F, "433 MHz must select the mid-band matching path");
    require(high.sw0 && high.iocfg2 == 0x6F, "915 MHz must select the high-band matching path");
    require(paTableValue(315.0F, 0) == 0x51, "315 MHz must use its own PA table column");
    require(paTableValue(433.92F, 0) == 0x60, "433 MHz must use its own PA table column");
    require(paTableValue(868.0F, 10) == 0xC2, "legacy 868 MHz / 10 dBm must keep its PA register value");
    require(paTableValue(868.0F, 0) == 0x50, "changing the 868 MHz power must select a different PA value");
    require(paTableValue(915.0F, 0) == 0x8E, "915 MHz must use its own PA table column");
    require(!paTableValue(868.0F, 22), "power values outside the Cap presets must be rejected");
    return 0;
}
