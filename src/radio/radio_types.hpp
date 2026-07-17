#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cc1101_chat::radio {

inline constexpr std::size_t kMaxPayloadSize = 61;

enum class RadioState {
    Stopped,
    Initializing,
    Idle,
    Receiving,
    Sending,
    Error,
    Stopping,
};

struct RadioInfo {
    std::string backend_name;
    bool mock               = false;
    uint8_t chip_version    = 0;
    float frequency_mhz     = 868.0F;
    float bit_rate_kbps     = 2.4F;
    float rx_bandwidth_khz  = 58.0F;
    float deviation_khz     = 25.4F;
    int8_t output_power_dbm = 10;
};

struct RadioPacket {
    uint64_t sequence     = 0;
    uint64_t timestamp_ms = 0;
    std::vector<uint8_t> data;
    float rssi_dbm = 0.0F;
    uint8_t lqi    = 0;
    bool crc_ok    = false;
};

class RadioCancelled final : public std::exception {
public:
    const char* what() const noexcept override
    {
        return "radio operation cancelled";
    }
};

class CancellationToken {
public:
    CancellationToken() = default;
    explicit CancellationToken(const std::atomic_bool& cancelled) : _cancelled(&cancelled)
    {
    }

    bool stopRequested() const noexcept
    {
        return _cancelled != nullptr && _cancelled->load(std::memory_order_acquire);
    }

    void throwIfCancellationRequested() const
    {
        if (stopRequested()) {
            throw RadioCancelled{};
        }
    }

    const std::atomic_bool* nativeFlag() const noexcept
    {
        return _cancelled;
    }

private:
    const std::atomic_bool* _cancelled = nullptr;
};

struct RadioRetryCommand {};

struct RadioSetReceiveCommand {
    bool enabled = true;
};

struct RadioSendCommand {
    uint64_t id = 0;
    std::vector<uint8_t> payload;
};

struct RadioShutdownCommand {};

using RadioCommand = std::variant<RadioRetryCommand, RadioSetReceiveCommand, RadioSendCommand, RadioShutdownCommand>;

struct RadioStateEvent {
    RadioState state = RadioState::Stopped;
    std::string detail;
};

struct RadioInitializedEvent {
    RadioInfo info;
};

struct RadioErrorEvent {
    std::string operation;
    std::string message;
};

struct RadioRxPacketEvent {
    RadioPacket packet;
};

struct RadioTxStartedEvent {
    uint64_t id      = 0;
    std::size_t size = 0;
};

struct RadioTxCompletedEvent {
    uint64_t id = 0;
};

struct RadioTxFailedEvent {
    uint64_t id = 0;
    std::string message;
};

struct RadioQueueOverflowEvent {
    std::size_t dropped = 0;
};

using RadioEvent =
    std::variant<RadioStateEvent, RadioInitializedEvent, RadioErrorEvent, RadioRxPacketEvent, RadioTxStartedEvent,
                 RadioTxCompletedEvent, RadioTxFailedEvent, RadioQueueOverflowEvent>;

}  // namespace cc1101_chat::radio
