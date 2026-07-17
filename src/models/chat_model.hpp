#pragma once

#include "core/cc1101_chat_types.hpp"
#include <memory>
#include <string>
#include <tools/observable/single_observable.hpp>
#include <unordered_map>

namespace cc1101_chat::radio {
class RadioWorker;
}

namespace cc1101_chat {

class ChatModel {
public:
    ChatModel();
    ~ChatModel();

    ChatModel(const ChatModel&)            = delete;
    ChatModel& operator=(const ChatModel&) = delete;

    void start();
    void stop();
    void tick(uint32_t nowMs);

    smooth_ui_toolkit::SingleObservable<std::vector<ChatMessage>>& messages()
    {
        return _messages;
    }

    smooth_ui_toolkit::SingleObservable<ChatRadioInfo>& radioInfo()
    {
        return _radio_info;
    }

    smooth_ui_toolkit::SingleObservable<std::string>& draft()
    {
        return _draft;
    }

    smooth_ui_toolkit::SingleObservable<std::string>& composeStatus()
    {
        return _compose_status;
    }

    void beginCompose(char firstCharacter = '\0');
    void setDraft(std::string value);
    void appendDraft(char character);
    void eraseDraftCharacter();
    void clearDraft();
    bool sendDraft();
    bool retryRadio();

private:
    std::unique_ptr<radio::RadioWorker> _radio_worker;
    smooth_ui_toolkit::SingleObservable<std::vector<ChatMessage>> _messages{std::vector<ChatMessage>{}};
    smooth_ui_toolkit::SingleObservable<ChatRadioInfo> _radio_info{ChatRadioInfo{}};
    smooth_ui_toolkit::SingleObservable<std::string> _draft{""};
    smooth_ui_toolkit::SingleObservable<std::string> _compose_status{""};
    uint64_t _next_message_id = 1;
    uint64_t _next_tx_id      = 1;
    std::unordered_map<uint64_t, std::string> _pending_messages;
    bool _started = false;

    void appendMessage(ChatMessage message);
    void setComposeStatus(std::string status);
};

}  // namespace cc1101_chat
