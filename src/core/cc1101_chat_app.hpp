#pragma once

#include "core/cc1101_chat_router.hpp"
#include "models/chat_model.hpp"
#include "view_models/chat_view_model.hpp"
#include "views/chat_view.hpp"
#include "views/view.hpp"
#include <array>
#include <lvgl.h>

namespace cc1101_chat {

class CC1101ChatApp {
public:
    CC1101ChatApp();
    ~CC1101ChatApp();

    CC1101ChatApp(const CC1101ChatApp&)            = delete;
    CC1101ChatApp& operator=(const CC1101ChatApp&) = delete;

    void start();
    void stop();
    void onKey(uint32_t key);
    bool onLvglKeyState(uint32_t lvKey, const char* utf8, bool pressed);
    void tick(uint32_t nowMs);

    bool quitRequested() const
    {
        return _quit_requested;
    }

private:
    CC1101ChatRouter _router;
    ChatModel _model;
    ChatViewModel _chat_vm;
    ChatView _chat_view;
    ViewModel* _current_vm    = nullptr;
    View* _current_view       = nullptr;
    lv_group_t* _input_group  = nullptr;
    size_t _route_observer_id = 0;
    bool _quit_requested      = false;
    bool _started             = false;

    std::array<ViewModel*, 1> _view_models;
    std::array<View*, 1> _views;

    ViewModel* viewModelFor(PageId page);
    View* viewFor(PageId page);
    void setupInputGroup();
    void setCurrentPage(PageId page);
    static void onRouteChanged(void* context, const PageId& page);
    static void onKeyboardEvent(lv_event_t* event);
};

}  // namespace cc1101_chat
