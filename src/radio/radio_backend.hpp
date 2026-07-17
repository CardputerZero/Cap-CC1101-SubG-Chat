#pragma once

#include "radio/radio_types.hpp"

#include <chrono>
#include <memory>
#include <vector>

namespace cc1101_chat::radio {

class RadioBackend {
public:
    virtual ~RadioBackend() = default;

    RadioBackend(const RadioBackend&)            = delete;
    RadioBackend& operator=(const RadioBackend&) = delete;

    virtual RadioInfo open(const CancellationToken& cancellation)                                     = 0;
    virtual void close() noexcept                                                                     = 0;
    virtual void startReceive(const CancellationToken& cancellation)                                  = 0;
    virtual void stopReceive()                                                                        = 0;
    virtual bool receive(RadioPacket& packet, std::chrono::milliseconds timeout,
                         const CancellationToken& cancellation)                                       = 0;
    virtual void transmit(const std::vector<uint8_t>& payload, const CancellationToken& cancellation) = 0;

protected:
    RadioBackend() = default;
};

std::unique_ptr<RadioBackend> makeMockRadioBackend();
std::unique_ptr<RadioBackend> makeLinuxCc1101Backend();
std::unique_ptr<RadioBackend> makeDefaultRadioBackend();

}  // namespace cc1101_chat::radio
