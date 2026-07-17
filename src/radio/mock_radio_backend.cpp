#include "radio/radio_backend.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace cc1101_chat::radio {
namespace {

using Clock = std::chrono::steady_clock;

uint64_t monotonicMilliseconds()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
}

long envLong(const char* name, long fallback, long minimum, long maximum)
{
    const char* text = std::getenv(name);
    if (!text || text[0] == '\0') {
        return fallback;
    }

    char* end        = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (!end || end == text || *end != '\0') {
        return fallback;
    }
    return std::clamp(value, minimum, maximum);
}

bool envBool(const char* name, bool fallback)
{
    const char* text = std::getenv(name);
    if (!text || text[0] == '\0') {
        return fallback;
    }
    const std::string value(text);
    return value != "0" && value != "false" && value != "off" && value != "no";
}

void cancellableSleepUntil(Clock::time_point deadline, const CancellationToken& cancellation)
{
    while (Clock::now() < deadline) {
        cancellation.throwIfCancellationRequested();
        const auto remaining = deadline - Clock::now();
        const auto slice =
            std::min(remaining, std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(10)));
        if (slice > Clock::duration::zero()) {
            std::this_thread::sleep_for(slice);
        }
    }
    cancellation.throwIfCancellationRequested();
}

class MockRadioBackend final : public RadioBackend {
public:
    RadioInfo open(const CancellationToken& cancellation) override
    {
        close();
        cancellableSleepUntil(Clock::now() + std::chrono::milliseconds(250), cancellation);
        if (envBool("CC1101_CHAT_MOCK_INIT_FAIL", false)) {
            throw std::runtime_error("mock initialization failure requested by environment");
        }

        _rx_interval           = std::chrono::milliseconds(envLong("CC1101_CHAT_MOCK_RX_INTERVAL_MS", 1600, 50, 60000));
        _crc_fail_every        = static_cast<uint64_t>(envLong("CC1101_CHAT_MOCK_CRC_FAIL_EVERY", 0, 0, 1000000));
        _loopback              = envBool("CC1101_CHAT_MOCK_LOOPBACK", true);
        _open                  = true;
        _next_generated_packet = Clock::now() + _rx_interval;

        RadioInfo info;
        info.backend_name     = "Mock CC1101";
        info.mock             = true;
        info.chip_version     = 0x14;
        info.frequency_mhz    = 868.0F;
        info.bit_rate_kbps    = 2.4F;
        info.rx_bandwidth_khz = 58.0F;
        info.deviation_khz    = 25.4F;
        info.output_power_dbm = 10;
        return info;
    }

    void close() noexcept override
    {
        _open      = false;
        _receiving = false;
        _pending_loopback.reset();
    }

    void startReceive(const CancellationToken& cancellation) override
    {
        cancellation.throwIfCancellationRequested();
        requireOpen();
        _receiving = true;
        if (_next_generated_packet < Clock::now()) {
            _next_generated_packet = Clock::now() + _rx_interval;
        }
    }

    void stopReceive() override
    {
        _receiving = false;
    }

    bool receive(RadioPacket& packet, std::chrono::milliseconds timeout, const CancellationToken& cancellation) override
    {
        cancellation.throwIfCancellationRequested();
        requireOpen();
        if (!_receiving) {
            return false;
        }

        const auto deadline = Clock::now() + std::max(timeout, std::chrono::milliseconds::zero());
        while (true) {
            cancellation.throwIfCancellationRequested();
            const auto now = Clock::now();
            if (_pending_loopback && now >= _pending_loopback->ready_at) {
                fillPacket(packet, std::move(_pending_loopback->payload));
                _pending_loopback.reset();
                return true;
            }
            if (now >= _next_generated_packet) {
                const std::string text = "mock-" + std::to_string(_next_sequence + 1);
                fillPacket(packet, std::vector<uint8_t>(text.begin(), text.end()));
                _next_generated_packet = now + _rx_interval;
                return true;
            }
            if (now >= deadline) {
                return false;
            }

            auto wake = std::min(deadline, _next_generated_packet);
            if (_pending_loopback) {
                wake = std::min(wake, _pending_loopback->ready_at);
            }
            cancellableSleepUntil(std::min(wake, now + std::chrono::milliseconds(10)), cancellation);
        }
    }

    void transmit(const std::vector<uint8_t>& payload, const CancellationToken& cancellation) override
    {
        cancellation.throwIfCancellationRequested();
        requireOpen();
        if (payload.empty()) {
            throw std::runtime_error("payload is empty");
        }
        if (payload.size() > kMaxPayloadSize) {
            throw std::runtime_error("payload exceeds the 61-byte receive-safe limit");
        }
        if (envBool("CC1101_CHAT_MOCK_TX_FAIL", false)) {
            throw std::runtime_error("mock transmit failure requested by environment");
        }

        const auto airtime_ms = 150 + static_cast<int>(((payload.size() + 24U) * 8U) / 2.4F);
        cancellableSleepUntil(Clock::now() + std::chrono::milliseconds(airtime_ms), cancellation);
        if (_loopback) {
            _pending_loopback = PendingLoopback{Clock::now() + std::chrono::milliseconds(100), payload};
        }
    }

private:
    struct PendingLoopback {
        Clock::time_point ready_at;
        std::vector<uint8_t> payload;
    };

    bool _open               = false;
    bool _receiving          = false;
    bool _loopback           = true;
    uint64_t _next_sequence  = 0;
    uint64_t _crc_fail_every = 0;
    std::chrono::milliseconds _rx_interval{1600};
    Clock::time_point _next_generated_packet{};
    std::optional<PendingLoopback> _pending_loopback;

    void requireOpen() const
    {
        if (!_open) {
            throw std::runtime_error("mock radio is not open");
        }
    }

    void fillPacket(RadioPacket& packet, std::vector<uint8_t> payload)
    {
        ++_next_sequence;
        packet.sequence     = _next_sequence;
        packet.timestamp_ms = monotonicMilliseconds();
        packet.data         = std::move(payload);
        packet.rssi_dbm     = -38.0F - static_cast<float>(_next_sequence % 12U) * 1.5F;
        packet.lqi          = static_cast<uint8_t>(2U + (_next_sequence * 7U) % 45U);
        packet.crc_ok       = _crc_fail_every == 0 || (_next_sequence % _crc_fail_every) != 0;
    }
};

}  // namespace

std::unique_ptr<RadioBackend> makeMockRadioBackend()
{
    return std::make_unique<MockRadioBackend>();
}

}  // namespace cc1101_chat::radio
