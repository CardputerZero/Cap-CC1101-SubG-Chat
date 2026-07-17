#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cc1101_chat {

namespace cc1101_chat_key {

constexpr uint32_t Up    = 0x10001;
constexpr uint32_t Down  = 0x10002;
constexpr uint32_t Left  = 0x10003;
constexpr uint32_t Right = 0x10004;

}  // namespace cc1101_chat_key

enum class PageId {
    Chat = 0,
};

enum class ChatSection {
    Messages,
    Info,
};

enum class RadioUiState {
    Initializing,
    Receiving,
    Sending,
    Error,
    Stopped,
};

struct ChatMessage {
    uint64_t id = 0;
    std::string text;
    bool outgoing   = false;
    float rssiDbm   = 0.0F;
    uint8_t lqi     = 0;
    bool crcOk      = true;
    bool sendFailed = false;
};

struct ChatRadioInfo {
    RadioUiState state        = RadioUiState::Initializing;
    bool ready                = false;
    bool initializationFailed = false;
    std::string spiDevice{"/dev/spidev0.1"};
    std::string chipVersion{"--"};
    float frequencyMhz    = 868.0F;
    float rssiDbm         = 0.0F;
    uint8_t lqi           = 0;
    bool lastCrcOk        = true;
    uint64_t rxCount      = 0;
    uint64_t txCount      = 0;
    uint64_t droppedCount = 0;
    std::string link{"868 MHz / 2-FSK low"};
    std::string diagnostics{"Starting radio"};
};

struct ChatScrollRequest {
    uint32_t serial = 0;
    int32_t amount  = 0;
    bool toBottom   = false;
};

constexpr std::size_t kMessageHistoryLimit = 64;
constexpr std::size_t kMaxMessageBytes     = 61;
constexpr int32_t kMessageScrollStep       = 36;

const char* pageIdName(PageId page);
const char* radioUiStateName(RadioUiState state);

}  // namespace cc1101_chat
