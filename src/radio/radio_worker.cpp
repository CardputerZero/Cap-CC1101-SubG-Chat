#include "radio/radio_worker.hpp"

#include "radio/chat_protocol.hpp"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

namespace cc1101_chat::radio {
namespace {

constexpr auto kReceiveSlice                  = std::chrono::milliseconds(40);
constexpr auto kAcknowledgementTimeout        = std::chrono::milliseconds(1800);
constexpr auto kAcknowledgementTurnaround     = std::chrono::milliseconds(240);
constexpr auto kPeerRxRecovery                = std::chrono::milliseconds(800);
constexpr std::size_t kMaximumSendAttempts    = 5;
constexpr std::size_t kAcknowledgementRepeats = 2;
constexpr std::size_t kRecentTokenCapacity    = 64;
constexpr uint32_t kProtocolTokenMask         = 0x00FFFFFFU;
static_assert(protocol::kHeaderSize + protocol::kMaxMessageSize == kMaxPayloadSize);

void cancellableSleep(std::chrono::milliseconds duration, const CancellationToken& cancellation)
{
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
        cancellation.throwIfCancellationRequested();
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        std::this_thread::sleep_for(
            std::min(std::chrono::milliseconds(10), std::max(remaining, std::chrono::milliseconds(1))));
    }
    cancellation.throwIfCancellationRequested();
}

uint32_t randomProtocolToken()
{
    uint32_t value = static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count() &
                                           static_cast<int64_t>(kProtocolTokenMask));
    try {
        std::random_device random;
        value ^= static_cast<uint32_t>(random());
        value ^= static_cast<uint32_t>(random()) << 8U;
    } catch (...) {
    }
    value &= kProtocolTokenMask;
    return value == 0 ? 1 : value;
}

std::chrono::milliseconds retryBackoff(uint32_t token, std::size_t attempt)
{
    const uint32_t mixed = token ^ static_cast<uint32_t>(attempt * 0x45D9F3U);
    return std::chrono::milliseconds(80 + static_cast<int>(mixed % 121U));
}

std::string exceptionMessage(const std::exception& exception)
{
    const char* message = exception.what();
    return message && message[0] != '\0' ? message : "unknown radio error";
}

}  // namespace

RadioWorker::RadioWorker() : RadioWorker(makeDefaultRadioBackend())
{
}

RadioWorker::RadioWorker(std::unique_ptr<RadioBackend> backend) : _backend(std::move(backend))
{
    if (!_backend) {
        throw std::invalid_argument("RadioWorker requires a backend");
    }
}

RadioWorker::~RadioWorker()
{
    stop();
}

bool RadioWorker::start(bool receive_on_start)
{
    std::lock_guard<std::mutex> lifecycle_lock(_lifecycle_mutex);
    if (_running.load(std::memory_order_acquire)) {
        return false;
    }
    if (_thread.joinable()) {
        _thread.join();
    }

    {
        std::lock_guard<std::mutex> command_lock(_command_mutex);
        _commands.clear();
    }
    {
        std::lock_guard<std::mutex> event_lock(_event_mutex);
        _events.clear();
        _pending_dropped_events = 0;
    }

    _receive_on_start       = receive_on_start;
    _initialization_attempt = 0;
    _next_protocol_token    = randomProtocolToken();
    _recent_rx_tokens.clear();
    _stop_requested.store(false, std::memory_order_release);
    _running.store(true, std::memory_order_release);
    try {
        _thread = std::thread(&RadioWorker::run, this);
    } catch (...) {
        _running.store(false, std::memory_order_release);
        throw;
    }
    return true;
}

void RadioWorker::stop()
{
    std::lock_guard<std::mutex> lifecycle_lock(_lifecycle_mutex);
    _stop_requested.store(true, std::memory_order_release);
    _command_cv.notify_all();
    if (_thread.joinable()) {
        _thread.join();
    }
    _running.store(false, std::memory_order_release);
}

RadioPostResult RadioWorker::post(RadioCommand command)
{
    if (auto* send = std::get_if<RadioSendCommand>(&command)) {
        if (send->payload.empty()) {
            return RadioPostResult::EmptyPayload;
        }
        if (send->payload.size() > protocol::kMaxMessageSize) {
            return RadioPostResult::PayloadTooLarge;
        }
    }

    if (!_running.load(std::memory_order_acquire)) {
        return RadioPostResult::NotRunning;
    }
    if (std::holds_alternative<RadioShutdownCommand>(command)) {
        _stop_requested.store(true, std::memory_order_release);
        _command_cv.notify_all();
        return RadioPostResult::Accepted;
    }

    {
        std::lock_guard<std::mutex> lock(_command_mutex);
        if (_commands.size() >= kCommandQueueCapacity) {
            return RadioPostResult::QueueFull;
        }
        _commands.push_back(std::move(command));
    }
    _command_cv.notify_one();
    return RadioPostResult::Accepted;
}

bool RadioWorker::tryPopEvent(RadioEvent& event)
{
    std::lock_guard<std::mutex> lock(_event_mutex);
    if (_pending_dropped_events != 0) {
        event                   = RadioQueueOverflowEvent{_pending_dropped_events};
        _pending_dropped_events = 0;
        return true;
    }
    if (_events.empty()) {
        return false;
    }
    event = std::move(_events.front());
    _events.pop_front();
    return true;
}

std::size_t RadioWorker::pendingEventCount() const
{
    std::lock_guard<std::mutex> lock(_event_mutex);
    return _events.size() + (_pending_dropped_events != 0 ? 1U : 0U);
}

void RadioWorker::run()
{
    const CancellationToken cancellation(_stop_requested);
    bool initialized       = false;
    bool receive_requested = _receive_on_start;

    try {
        initialized = initializeBackend(receive_requested, cancellation);
        while (!_stop_requested.load(std::memory_order_acquire)) {
            RadioCommand command;
            if (tryPopCommand(command)) {
                handleCommand(std::move(command), initialized, receive_requested, cancellation);
                continue;
            }

            if (!initialized) {
                std::unique_lock<std::mutex> lock(_command_mutex);
                _command_cv.wait(
                    lock, [this]() { return _stop_requested.load(std::memory_order_acquire) || !_commands.empty(); });
                continue;
            }

            if (receive_requested) {
                RadioPacket packet;
                try {
                    if (_backend->receive(packet, kReceiveSlice, cancellation)) {
                        (void)processReceivedPacket(std::move(packet), 0, receive_requested, cancellation);
                    }
                } catch (const RadioCancelled&) {
                    throw;
                } catch (const std::exception& exception) {
                    pushEvent(RadioErrorEvent{"receive", exceptionMessage(exception)});
                    _backend->close();
                    initialized = false;
                    pushState(RadioState::Error, "Receive failed; retry required");
                }
            } else {
                std::unique_lock<std::mutex> lock(_command_mutex);
                _command_cv.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                    return _stop_requested.load(std::memory_order_acquire) || !_commands.empty();
                });
            }
        }
    } catch (const RadioCancelled&) {
    } catch (const std::exception& exception) {
        if (!_stop_requested.load(std::memory_order_acquire)) {
            pushEvent(RadioErrorEvent{"worker", exceptionMessage(exception)});
            pushState(RadioState::Error, "Radio worker stopped unexpectedly");
        }
    }

    pushState(RadioState::Stopping, "Stopping radio");
    _backend->close();
    pushState(RadioState::Stopped, "Radio stopped");
    _running.store(false, std::memory_order_release);
}

bool RadioWorker::initializeBackend(bool receive_requested, const CancellationToken& cancellation)
{
    const std::size_t attempt      = ++_initialization_attempt;
    const auto started_at          = std::chrono::steady_clock::now();
    const auto elapsedMilliseconds = [&started_at]() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_at)
            .count();
    };

    spdlog::info("CC1101 radio: initialization attempt {} (receive_on_start={})", attempt, receive_requested);
    pushState(RadioState::Initializing, "Initializing CC1101");
    _backend->close();
    try {
        RadioInfo info = _backend->open(cancellation);
        spdlog::info(
            "CC1101 radio: backend open after {} ms (backend={}, version=0x{:02X}, frequency={:.3f} MHz, "
            "bitrate={:.3f} kbps, bandwidth={:.1f} kHz, deviation={:.1f} kHz, power={} dBm)",
            elapsedMilliseconds(), info.backend_name, static_cast<unsigned>(info.chip_version), info.frequency_mhz,
            info.bit_rate_kbps, info.rx_bandwidth_khz, info.deviation_khz, static_cast<int>(info.output_power_dbm));
        pushEvent(RadioInitializedEvent{std::move(info)});
        if (receive_requested) {
            spdlog::info("CC1101 radio: starting receive mode");
            _backend->startReceive(cancellation);
            pushState(RadioState::Receiving, "Receiving");
        } else {
            pushState(RadioState::Idle, "Radio idle");
        }
        spdlog::info("CC1101 radio: initialization attempt {} completed in {} ms", attempt, elapsedMilliseconds());
        return true;
    } catch (const RadioCancelled&) {
        spdlog::debug("CC1101 radio: initialization attempt {} cancelled after {} ms", attempt, elapsedMilliseconds());
        throw;
    } catch (const std::exception& exception) {
        spdlog::error("CC1101 radio: initialization attempt {} failed after {} ms: {}", attempt, elapsedMilliseconds(),
                      exceptionMessage(exception));
        _backend->close();
        pushEvent(RadioErrorEvent{"initialize", exceptionMessage(exception)});
        pushState(RadioState::Error, "Initialization failed; retry required");
        return false;
    }
}

bool RadioWorker::tryPopCommand(RadioCommand& command)
{
    std::lock_guard<std::mutex> lock(_command_mutex);
    if (_commands.empty()) {
        return false;
    }
    command = std::move(_commands.front());
    _commands.pop_front();
    return true;
}

void RadioWorker::handleCommand(RadioCommand command, bool& initialized, bool& receive_requested,
                                const CancellationToken& cancellation)
{
    if (std::holds_alternative<RadioRetryCommand>(command)) {
        initialized = initializeBackend(receive_requested, cancellation);
        return;
    }
    if (auto* set_receive = std::get_if<RadioSetReceiveCommand>(&command)) {
        receive_requested = set_receive->enabled;
        if (!initialized) {
            return;
        }
        try {
            if (receive_requested) {
                _backend->startReceive(cancellation);
                pushState(RadioState::Receiving, "Receiving");
            } else {
                _backend->stopReceive();
                pushState(RadioState::Idle, "Radio idle");
            }
        } catch (const RadioCancelled&) {
            throw;
        } catch (const std::exception& exception) {
            pushEvent(RadioErrorEvent{"set receive", exceptionMessage(exception)});
            _backend->close();
            initialized = false;
            pushState(RadioState::Error, "Receive mode change failed; retry required");
        }
        return;
    }
    if (auto* send = std::get_if<RadioSendCommand>(&command)) {
        handleSend(std::move(*send), initialized, receive_requested, cancellation);
    }
}

void RadioWorker::handleSend(RadioSendCommand command, bool& initialized, bool receive_requested,
                             const CancellationToken& cancellation)
{
    if (!initialized) {
        pushEvent(RadioTxFailedEvent{command.id, "radio is not initialized"});
        return;
    }
    if (command.payload.empty() || command.payload.size() > protocol::kMaxMessageSize) {
        pushEvent(RadioTxFailedEvent{command.id, "payload must contain 1 to 56 bytes"});
        return;
    }

    pushEvent(RadioTxStartedEvent{command.id, command.payload.size()});
    pushState(RadioState::Sending, "Sending");
    try {
        const uint32_t token             = nextProtocolToken();
        const std::vector<uint8_t> frame = protocol::encodeData(token, command.payload);
        bool acknowledged                = false;

        for (std::size_t attempt = 1; attempt <= kMaximumSendAttempts; ++attempt) {
            spdlog::info("CC1101 radio: sending message id={} token=0x{:06X} attempt={}/{} bytes={}", command.id, token,
                         attempt, kMaximumSendAttempts, command.payload.size());
            if (receive_requested) {
                _backend->stopReceive();
            }
            _backend->transmit(frame, cancellation);
            if (!receive_requested) {
                acknowledged = true;
                break;
            }

            _backend->startReceive(cancellation);
            if (waitForAcknowledgement(token, receive_requested, cancellation)) {
                acknowledged = true;
                cancellableSleep(kPeerRxRecovery, cancellation);
                break;
            }

            spdlog::warn("CC1101 radio: acknowledgement timeout id={} token=0x{:06X} attempt={}/{}", command.id, token,
                         attempt, kMaximumSendAttempts);
            if (attempt < kMaximumSendAttempts) {
                cancellableSleep(retryBackoff(token, attempt), cancellation);
            }
        }

        if (acknowledged) {
            pushEvent(RadioTxCompletedEvent{command.id});
        } else {
            pushEvent(RadioTxFailedEvent{command.id, "no acknowledgement after 5 attempts"});
        }
        if (receive_requested) {
            pushState(RadioState::Receiving, "Receiving");
        } else {
            pushState(RadioState::Idle, "Radio idle");
        }
    } catch (const RadioCancelled&) {
        throw;
    } catch (const std::exception& exception) {
        pushEvent(RadioTxFailedEvent{command.id, exceptionMessage(exception)});
        if (receive_requested) {
            try {
                _backend->startReceive(cancellation);
                pushState(RadioState::Receiving, "Receive restored after send failure");
                return;
            } catch (const RadioCancelled&) {
                throw;
            } catch (const std::exception& restore_exception) {
                pushEvent(RadioErrorEvent{"restore receive", exceptionMessage(restore_exception)});
            }
        }
        _backend->close();
        initialized = false;
        pushState(RadioState::Error, "Send failed; retry required");
    }
}

bool RadioWorker::waitForAcknowledgement(uint32_t token, bool receive_requested, const CancellationToken& cancellation)
{
    const auto deadline = std::chrono::steady_clock::now() + kAcknowledgementTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
        cancellation.throwIfCancellationRequested();
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        RadioPacket packet;
        if (_backend->receive(packet, std::min(kReceiveSlice, std::max(remaining, std::chrono::milliseconds(1))),
                              cancellation) &&
            processReceivedPacket(std::move(packet), token, receive_requested, cancellation)) {
            spdlog::info("CC1101 radio: acknowledgement received token=0x{:06X}", token);
            return true;
        }
    }
    return false;
}

bool RadioWorker::processReceivedPacket(RadioPacket packet, uint32_t expected_ack, bool receive_requested,
                                        const CancellationToken& cancellation)
{
    if (!packet.crc_ok) {
        spdlog::warn("CC1101 radio: discarded packet with bad CRC (bytes={}, RSSI={:.1f}, LQI={})", packet.data.size(),
                     packet.rssi_dbm, static_cast<unsigned>(packet.lqi));
        return false;
    }

    protocol::DecodedFrame frame = protocol::decode(packet.data);
    switch (frame.kind) {
        case protocol::FrameKind::Legacy:
            pushEvent(RadioRxPacketEvent{std::move(packet)});
            return false;
        case protocol::FrameKind::Malformed:
            spdlog::warn("CC1101 radio: discarded malformed chat frame (bytes={})", packet.data.size());
            return false;
        case protocol::FrameKind::Acknowledgement:
            if (expected_ack != 0 && frame.token == expected_ack) {
                return true;
            }
            spdlog::debug("CC1101 radio: ignored stale acknowledgement token=0x{:06X}", frame.token);
            return false;
        case protocol::FrameKind::Data:
            break;
    }

    const bool duplicate = recentlyReceived(frame.token);
    if (!duplicate) {
        rememberReceived(frame.token);
        packet.data = std::move(frame.payload);
        pushEvent(RadioRxPacketEvent{std::move(packet)});
    }

    if (receive_requested) {
        acknowledge(frame.token, cancellation);
    }
    if (duplicate) {
        spdlog::info("CC1101 radio: acknowledged duplicate message token=0x{:06X}", frame.token);
    }
    return false;
}

void RadioWorker::acknowledge(uint32_t token, const CancellationToken& cancellation)
{
    cancellableSleep(kAcknowledgementTurnaround, cancellation);
    _backend->stopReceive();
    const auto acknowledgement = protocol::encodeAcknowledgement(token);
    for (std::size_t repeat = 0; repeat < kAcknowledgementRepeats; ++repeat) {
        spdlog::debug("CC1101 radio: transmitting acknowledgement token=0x{:06X} repeat={}/{}", token, repeat + 1,
                      kAcknowledgementRepeats);
        _backend->transmit(acknowledgement, cancellation);
    }
    _backend->startReceive(cancellation);
}

uint32_t RadioWorker::nextProtocolToken()
{
    const uint32_t token = _next_protocol_token;
    _next_protocol_token = (_next_protocol_token + 1U) & kProtocolTokenMask;
    if (_next_protocol_token == 0) {
        _next_protocol_token = 1;
    }
    return token;
}

bool RadioWorker::recentlyReceived(uint32_t token) const
{
    return std::find(_recent_rx_tokens.begin(), _recent_rx_tokens.end(), token) != _recent_rx_tokens.end();
}

void RadioWorker::rememberReceived(uint32_t token)
{
    if (_recent_rx_tokens.size() >= kRecentTokenCapacity) {
        _recent_rx_tokens.pop_front();
    }
    _recent_rx_tokens.push_back(token);
}

void RadioWorker::pushEvent(RadioEvent event)
{
    std::lock_guard<std::mutex> lock(_event_mutex);
    if (_events.size() >= kEventQueueCapacity) {
        const auto rx = std::find_if(_events.begin(), _events.end(), [](const RadioEvent& queued) {
            return std::holds_alternative<RadioRxPacketEvent>(queued);
        });
        if (rx != _events.end()) {
            _events.erase(rx);
        } else {
            _events.pop_front();
        }
        ++_pending_dropped_events;
    }
    _events.push_back(std::move(event));
}

void RadioWorker::pushState(RadioState state, std::string detail)
{
    pushEvent(RadioStateEvent{state, std::move(detail)});
}

}  // namespace cc1101_chat::radio
