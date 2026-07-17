#include "view_models/chat_view_model.hpp"

#include <cctype>
#include <utility>

namespace cc1101_chat {
namespace {

bool isPrintableAscii(uint32_t key)
{
    return key >= 0x20 && key <= 0x7e;
}

bool isPreviousKey(uint32_t key)
{
    return key == cc1101_chat_key::Left || key == 'z' || key == 'Z';
}

bool isNextKey(uint32_t key)
{
    return key == cc1101_chat_key::Right || key == 'c' || key == 'C';
}

}  // namespace

ChatViewModel::ChatViewModel(CC1101ChatRouter& router, ChatModel& model) : ViewModel(router), _model(model)
{
}

void ChatViewModel::onEnter()
{
    _model.clearDraft();
    _compose_active.set(false);
    const auto& info = _model.radioInfo().get();
    _section.set(info.state == RadioUiState::Error ? ChatSection::Info : ChatSection::Messages);
    _last_state                 = info.state;
    _last_initialization_failed = info.state == RadioUiState::Error && info.initializationFailed;
    _initialization_dialog_active.set(_last_initialization_failed);
}

void ChatViewModel::onExit()
{
    if (_compose_active.get()) {
        _model.clearDraft();
        _compose_active.set(false);
    }
    _initialization_dialog_active.set(false);
}

void ChatViewModel::onKey(uint32_t key)
{
    if (_initialization_dialog_active.get()) {
        if (key == '\x1b') {
            dismissInitializationDialog();
        } else if (key == '\r' || key == 'r' || key == 'R') {
            retryRadio();
        }
        return;
    }

    if (_compose_active.get()) {
        if (key == '\x1b') {
            cancelCompose();
        } else if (key == '\b' || key == 0x7f) {
            _model.eraseDraftCharacter();
        } else if (key == '\r') {
            sendCompose();
        } else if (isPrintableAscii(key)) {
            _model.appendDraft(static_cast<char>(key));
        }
        return;
    }

    if (_section.get() == ChatSection::Info && _model.radioInfo().get().state == RadioUiState::Error &&
        (key == '\r' || key == 'r' || key == 'R')) {
        retryRadio();
        return;
    }

    if (isPreviousKey(key)) {
        _section.set(ChatSection::Messages);
        return;
    }
    if (isNextKey(key)) {
        _section.set(ChatSection::Info);
        return;
    }

    if (key == cc1101_chat_key::Up) {
        if (_section.get() == ChatSection::Messages) {
            requestScroll(kMessageScrollStep);
        } else {
            _section.set(ChatSection::Messages);
        }
        return;
    }
    if (key == cc1101_chat_key::Down) {
        if (_section.get() == ChatSection::Messages) {
            requestScroll(-kMessageScrollStep);
        } else {
            _section.set(ChatSection::Info);
        }
        return;
    }

    if (key == '\r') {
        openCompose('\0');
        return;
    }

    if (isPrintableAscii(key) && key != 'z' && key != 'Z' && key != 'c' && key != 'C') {
        openCompose(static_cast<char>(key));
    }
}

void ChatViewModel::tick(uint32_t nowMs)
{
    _model.tick(nowMs);
    const auto& info                = _model.radioInfo().get();
    const RadioUiState state        = info.state;
    const bool initializationFailed = state == RadioUiState::Error && info.initializationFailed;
    if (state == RadioUiState::Error && _last_state != RadioUiState::Error) {
        _section.set(ChatSection::Info);
    }
    if (initializationFailed && !_last_initialization_failed) {
        if (_compose_active.get()) {
            cancelCompose();
        }
        _section.set(ChatSection::Info);
        _initialization_dialog_active.set(true);
    }
    if (info.ready && _initialization_dialog_active.get()) {
        _initialization_dialog_active.set(false);
    }
    _last_state                 = state;
    _last_initialization_failed = initializationFailed;
}

void ChatViewModel::openCompose(char firstCharacter)
{
    _model.beginCompose(firstCharacter);
    _compose_active.set(true);
}

void ChatViewModel::setDraft(std::string draft)
{
    _model.setDraft(std::move(draft));
}

void ChatViewModel::cancelCompose()
{
    _model.clearDraft();
    _compose_active.set(false);
}

void ChatViewModel::sendCompose()
{
    if (_model.sendDraft()) {
        _section.set(ChatSection::Messages);
        requestScrollToBottom();
        _compose_active.set(false);
    }
}

void ChatViewModel::dismissInitializationDialog()
{
    _initialization_dialog_active.set(false);
}

void ChatViewModel::retryRadio()
{
    _last_initialization_failed = false;
    if (_model.retryRadio()) {
        _initialization_dialog_active.set(false);
    } else {
        _initialization_dialog_active.set(true);
    }
}

void ChatViewModel::requestScroll(int32_t amount)
{
    _scroll_request.set(ChatScrollRequest{++_scroll_serial, amount});
}

void ChatViewModel::requestScrollToBottom()
{
    _scroll_request.set(ChatScrollRequest{++_scroll_serial, 0, true});
}

}  // namespace cc1101_chat
