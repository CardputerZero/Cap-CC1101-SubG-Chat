#pragma once

#include "view_models/chat_view_model.hpp"
#include "views/view.hpp"

#include <lvgl/lvgl_cpp/obj.hpp>
#include <memory>

namespace cc1101_chat {

class ChatView : public View {
public:
    explicit ChatView(ChatViewModel& viewModel);
    ~ChatView() override;

    void onEnter(lv_obj_t* parent) override;
    void onExit() override;
    void tick(uint32_t nowMs) override;

private:
    class ChatPager;

    ChatViewModel& _view_model;

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _root;
    std::unique_ptr<ChatPager> _pager;
    uint32_t _scroll_serial_seen = 0;
    bool _messages_tip_shown     = false;

    static void onMessagesChanged(void* context, const std::vector<ChatMessage>& messages);
    static void onRadioInfoChanged(void* context, const ChatRadioInfo& info);
    static void onSectionChanged(void* context, const ChatSection& section);
    static void onScrollRequestChanged(void* context, const ChatScrollRequest& request);
    static void onDraftChanged(void* context, const std::string& draft);
    static void onComposeStatusChanged(void* context, const std::string& status);
    static void onComposeActiveChanged(void* context, const bool& active);
    static void onInitializationDialogActiveChanged(void* context, const bool& active);
};

}  // namespace cc1101_chat
