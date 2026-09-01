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
    return 0;
}
