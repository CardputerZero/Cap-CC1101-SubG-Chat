#pragma once

#include "models/chat_model.hpp"
#include "view_models/view_model.hpp"
#include <tools/observable/single_observable.hpp>

namespace cc1101_chat {

class ChatViewModel : public ViewModel {
public:
    ChatViewModel(CC1101ChatRouter& router, ChatModel& model);

    PageId pageId() const override
    {
        return PageId::Chat;
    }

    void onEnter() override;
    void onExit() override;
    void onKey(uint32_t key) override;
    void tick(uint32_t nowMs) override;

    smooth_ui_toolkit::SingleObservable<std::vector<ChatMessage>>& messages()
    {
        return _model.messages();
    }

    smooth_ui_toolkit::SingleObservable<ChatRadioInfo>& radioInfo()
    {
        return _model.radioInfo();
    }

    smooth_ui_toolkit::SingleObservable<ChatSection>& section()
    {
        return _section;
    }

    smooth_ui_toolkit::SingleObservable<ChatScrollRequest>& scrollRequest()
    {
        return _scroll_request;
    }

    smooth_ui_toolkit::SingleObservable<std::string>& draft()
    {
        return _model.draft();
    }

    smooth_ui_toolkit::SingleObservable<std::string>& composeStatus()
    {
        return _model.composeStatus();
    }

    smooth_ui_toolkit::SingleObservable<bool>& composeActive()
    {
        return _compose_active;
    }

    smooth_ui_toolkit::SingleObservable<bool>& initializationDialogActive()
    {
        return _initialization_dialog_active;
    }

    bool modalActive() const
    {
        return _compose_active.get() || _initialization_dialog_active.get();
    }

    void setDraft(std::string draft);
    void cancelCompose();
    void sendCompose();
    void dismissInitializationDialog();
    void retryRadio();

private:
    ChatModel& _model;
    smooth_ui_toolkit::SingleObservable<ChatSection> _section{ChatSection::Messages};
    smooth_ui_toolkit::SingleObservable<ChatScrollRequest> _scroll_request{ChatScrollRequest{}};
    smooth_ui_toolkit::SingleObservable<bool> _compose_active{false};
    smooth_ui_toolkit::SingleObservable<bool> _initialization_dialog_active{false};
    uint32_t _scroll_serial          = 0;
    RadioUiState _last_state         = RadioUiState::Initializing;
    bool _last_initialization_failed = false;

    void openCompose(char firstCharacter);
    void requestScroll(int32_t amount);
    void requestScrollToBottom();
};

}  // namespace cc1101_chat
