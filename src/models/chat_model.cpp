#include "models/chat_model.hpp"

#include "radio/radio_types.hpp"
#include "radio/radio_worker.hpp"
#include <algorithm>
#include <cstdio>
#include <exception>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <utility>
#include <variant>

namespace cc1101_chat {
namespace {

constexpr std::size_t kMaxEventsPerTick = 16;

std::string printablePayload(const std::vector<uint8_t>& payload)
{
    if (payload.empty()) {
        return "<empty>";
    }

    std::string text;
    text.reserve(payload.size());
    for (uint8_t byte : payload) {
        text.push_back(byte >= 0x20 && byte <= 0x7e ? static_cast<char>(byte) : '.');
    }
    return text;
}

std::string versionText(uint8_t version)
{
    std::ostringstream stream;
    stream << "v0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<unsigned int>(version);
    return stream.str();
}

std::string linkText(const radio::RadioInfo& info)
{
    char buffer[128] = {};
    std::snprintf(buffer, sizeof(buffer), "%.0fM 2-FSK %.1fk BW%.0f DEV%.1f SW12AD", info.frequency_mhz,
                  info.bit_rate_kbps, info.rx_bandwidth_khz, info.deviation_khz);
    return buffer;
}

const char* postResultText(radio::RadioPostResult result)
{
    switch (result) {
        case radio::RadioPostResult::Accepted:
            return "";
        case radio::RadioPostResult::NotRunning:
            return "Radio is not running";
        case radio::RadioPostResult::QueueFull:
            return "Send queue is full";
        case radio::RadioPostResult::EmptyPayload:
            return "Message is empty :(";
        case radio::RadioPostResult::PayloadTooLarge:
            return "Message is too long";
    }
    return "Send failed";
}

}  // namespace

ChatModel::ChatModel() : _radio_worker(std::make_unique<radio::RadioWorker>())
{
}

ChatModel::~ChatModel()
{
    stop();
}

void ChatModel::start()
{
    if (_started) {
        return;
    }

    _started = true;
    _messages.set(std::vector<ChatMessage>{});
    _pending_messages.clear();
    _next_message_id = 1;
    _next_tx_id      = 1;

    ChatRadioInfo info;
    info.state       = RadioUiState::Initializing;
    info.ready       = false;
    info.diagnostics = "Starting radio";
    _radio_info.set(std::move(info));

    try {
        if (!_radio_worker->start(true)) {
            auto failed                 = _radio_info.get();
            failed.state                = RadioUiState::Error;
            failed.ready                = false;
            failed.initializationFailed = false;
            failed.diagnostics          = "Radio worker is already running";
            _radio_info.set(std::move(failed));
        }
    } catch (const std::exception& exception) {
        auto failed                 = _radio_info.get();
        failed.state                = RadioUiState::Error;
        failed.ready                = false;
        failed.initializationFailed = true;
        failed.diagnostics          = exception.what();
        _radio_info.set(std::move(failed));
    }
}

void ChatModel::stop()
{
    if (!_started) {
        return;
    }

    _radio_worker->stop();
    _pending_messages.clear();
    auto info        = _radio_info.get();
    info.state       = RadioUiState::Stopped;
    info.ready       = false;
    info.diagnostics = "Radio stopped";
    _radio_info.set(std::move(info));
    _started = false;
}

void ChatModel::tick(uint32_t nowMs)
{
    (void)nowMs;
    if (!_started) {
        return;
    }

    for (std::size_t index = 0; index < kMaxEventsPerTick; ++index) {
        radio::RadioEvent event;
        if (!_radio_worker->tryPopEvent(event)) {
            break;
        }

        std::visit(
            [this](auto&& value) {
                using Event = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Event, radio::RadioStateEvent>) {
                    auto info = _radio_info.get();
                    const bool preserve_error =
                        value.state == radio::RadioState::Error && info.diagnostics.find(':') != std::string::npos;
                    const bool preserve_send_failure =
                        value.state == radio::RadioState::Receiving && info.diagnostics.rfind("Send failed:", 0) == 0;
                    if (!preserve_error && !preserve_send_failure) {
                        info.diagnostics = value.detail;
                    }
                    switch (value.state) {
                        case radio::RadioState::Initializing:
                            info.state                = RadioUiState::Initializing;
                            info.ready                = false;
                            info.initializationFailed = false;
                            break;
                        case radio::RadioState::Idle:
                        case radio::RadioState::Receiving:
                            info.state                = RadioUiState::Receiving;
                            info.ready                = true;
                            info.initializationFailed = false;
                            break;
                        case radio::RadioState::Sending:
                            info.state                = RadioUiState::Sending;
                            info.ready                = true;
                            info.initializationFailed = false;
                            break;
                        case radio::RadioState::Error:
                            info.state = RadioUiState::Error;
                            info.ready = false;
                            break;
                        case radio::RadioState::Stopped:
                        case radio::RadioState::Stopping:
                            info.state = RadioUiState::Error;
                            info.ready = false;
                            if (value.detail.empty() || value.detail == "Radio stopped" ||
                                value.detail == "Stopping radio") {
                                info.diagnostics = "Radio worker stopped; press R to retry";
                            }
                            break;
                    }
                    _radio_info.set(std::move(info));
                } else if constexpr (std::is_same_v<Event, radio::RadioInitializedEvent>) {
                    auto info                 = _radio_info.get();
                    info.ready                = true;
                    info.initializationFailed = false;
                    info.spiDevice            = value.info.backend_name;
                    info.chipVersion          = versionText(value.info.chip_version);
                    info.frequencyMhz         = value.info.frequency_mhz;
                    info.link                 = linkText(value.info);
                    info.diagnostics          = value.info.mock ? "SDL mock radio ready" : "CC1101 ready";
                    _radio_info.set(std::move(info));
                } else if constexpr (std::is_same_v<Event, radio::RadioErrorEvent>) {
                    auto info                 = _radio_info.get();
                    info.initializationFailed = value.operation == "initialize";
                    info.diagnostics          = value.operation + ": " + value.message;
                    _radio_info.set(std::move(info));
                } else if constexpr (std::is_same_v<Event, radio::RadioRxPacketEvent>) {
                    auto info      = _radio_info.get();
                    info.rssiDbm   = value.packet.rssi_dbm;
                    info.lqi       = value.packet.lqi;
                    info.lastCrcOk = value.packet.crc_ok;
                    ++info.rxCount;
                    info.diagnostics = value.packet.crc_ok ? "Packet received" : "Packet received with bad CRC";
                    _radio_info.set(std::move(info));

                    ChatMessage message;
                    message.text     = printablePayload(value.packet.data);
                    message.outgoing = false;
                    message.rssiDbm  = value.packet.rssi_dbm;
                    message.lqi      = value.packet.lqi;
                    message.crcOk    = value.packet.crc_ok;
                    appendMessage(std::move(message));
                } else if constexpr (std::is_same_v<Event, radio::RadioTxStartedEvent>) {
                    auto info        = _radio_info.get();
                    info.state       = RadioUiState::Sending;
                    info.ready       = true;
                    info.diagnostics = "Sending " + std::to_string(value.size) + " bytes";
                    _radio_info.set(std::move(info));
                } else if constexpr (std::is_same_v<Event, radio::RadioTxCompletedEvent>) {
                    const auto pending = _pending_messages.find(value.id);
                    if (pending != _pending_messages.end()) {
                        ChatMessage message;
                        message.text     = std::move(pending->second);
                        message.outgoing = true;
                        appendMessage(std::move(message));
                        _pending_messages.erase(pending);
                    }
                    auto info = _radio_info.get();
                    ++info.txCount;
                    info.diagnostics = "Message sent";
                    _radio_info.set(std::move(info));
                } else if constexpr (std::is_same_v<Event, radio::RadioTxFailedEvent>) {
                    const auto pending = _pending_messages.find(value.id);
                    if (pending != _pending_messages.end()) {
                        ChatMessage message;
                        message.text       = std::move(pending->second);
                        message.outgoing   = true;
                        message.sendFailed = true;
                        appendMessage(std::move(message));
                        _pending_messages.erase(pending);
                    }
                    auto info        = _radio_info.get();
                    info.diagnostics = "Send failed: " + value.message;
                    _radio_info.set(std::move(info));
                } else if constexpr (std::is_same_v<Event, radio::RadioQueueOverflowEvent>) {
                    auto info = _radio_info.get();
                    info.droppedCount += value.dropped;
                    info.diagnostics = "Dropped " + std::to_string(value.dropped) + " queued events";
                    _radio_info.set(std::move(info));
                }
            },
            std::move(event));
    }
}

void ChatModel::beginCompose(char firstCharacter)
{
    std::string value;
    if (firstCharacter >= 0x20 && firstCharacter <= 0x7e) {
        value.push_back(firstCharacter);
    }
    _draft.set(std::move(value));
    setComposeStatus("");
}

void ChatModel::setDraft(std::string value)
{
    value.erase(std::remove_if(value.begin(), value.end(),
                               [](unsigned char character) { return character < 0x20 || character > 0x7e; }),
                value.end());
    if (value.size() > kMaxMessageBytes) {
        value.resize(kMaxMessageBytes);
    }
    _draft.set(std::move(value));
    setComposeStatus("");
}

void ChatModel::appendDraft(char character)
{
    if (character < 0x20 || character > 0x7e) {
        return;
    }
    std::string value = _draft.get();
    if (value.size() >= kMaxMessageBytes) {
        setComposeStatus("61 byte limit");
        return;
    }
    value.push_back(character);
    _draft.set(std::move(value));
    setComposeStatus("");
}

void ChatModel::eraseDraftCharacter()
{
    std::string value = _draft.get();
    if (!value.empty()) {
        value.pop_back();
        _draft.set(std::move(value));
    }
    setComposeStatus("");
}

void ChatModel::clearDraft()
{
    _draft.set("");
    setComposeStatus("");
}

bool ChatModel::sendDraft()
{
    const std::string message = _draft.get();
    if (message.empty()) {
        setComposeStatus("Message is empty :(");
        return false;
    }
    if (message.size() > kMaxMessageBytes) {
        setComposeStatus("Message is too long");
        return false;
    }
    if (!_radio_info.get().ready) {
        setComposeStatus("Radio is not ready");
        return false;
    }

    radio::RadioSendCommand command;
    command.id = _next_tx_id++;
    command.payload.assign(message.begin(), message.end());
    const radio::RadioPostResult result = _radio_worker->post(radio::RadioCommand{std::move(command)});
    if (result != radio::RadioPostResult::Accepted) {
        setComposeStatus(postResultText(result));
        return false;
    }

    _pending_messages.emplace(_next_tx_id - 1, message);
    _draft.set("");
    setComposeStatus("");
    return true;
}

bool ChatModel::retryRadio()
{
    if (!_started) {
        setComposeStatus("Radio is not running");
        return false;
    }

    auto info                 = _radio_info.get();
    info.state                = RadioUiState::Initializing;
    info.ready                = false;
    info.initializationFailed = false;
    info.diagnostics          = "Retrying radio";
    _radio_info.set(std::move(info));

    try {
        if (!_radio_worker->running()) {
            if (_radio_worker->start(true)) {
                return true;
            }
        } else {
            const radio::RadioPostResult result = _radio_worker->post(radio::RadioCommand{radio::RadioRetryCommand{}});
            if (result == radio::RadioPostResult::Accepted) {
                return true;
            }
            setComposeStatus(postResultText(result));
        }
    } catch (const std::exception& exception) {
        setComposeStatus(exception.what());
    }

    auto failed        = _radio_info.get();
    failed.state       = RadioUiState::Error;
    failed.ready       = false;
    failed.diagnostics = _compose_status.get().empty() ? "Retry failed" : _compose_status.get();
    _radio_info.set(std::move(failed));
    return false;
}

void ChatModel::appendMessage(ChatMessage message)
{
    message.id                       = _next_message_id++;
    std::vector<ChatMessage> history = _messages.get();
    if (history.size() >= kMessageHistoryLimit) {
        history.erase(history.begin());
    }
    history.push_back(std::move(message));
    _messages.set(std::move(history));
}

void ChatModel::setComposeStatus(std::string status)
{
    _compose_status.set(std::move(status));
}

}  // namespace cc1101_chat
