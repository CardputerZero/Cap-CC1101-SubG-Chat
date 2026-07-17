#include "core/cc1101_chat_app.hpp"

#include <spdlog/spdlog.h>

namespace cc1101_chat {
namespace {

bool isTextKey(const char* utf8, char expectedLowercase)
{
    if (!utf8 || utf8[0] == '\0' || utf8[1] != '\0') {
        return false;
    }
    return utf8[0] == expectedLowercase || utf8[0] == expectedLowercase - ('a' - 'A');
}

lv_obj_t* focusedTextInput()
{
    lv_indev_t* inputDevice = lv_indev_get_next(nullptr);
    while (inputDevice) {
        lv_group_t* group = lv_indev_get_group(inputDevice);
        if (group) {
            lv_obj_t* focused = lv_group_get_focused(group);
            if (focused && lv_obj_check_type(focused, &lv_textarea_class)) {
                return focused;
            }
        }
        inputDevice = lv_indev_get_next(inputDevice);
    }
    return nullptr;
}

#if LV_USE_SDL
bool textInputFocused()
{
    return focusedTextInput() != nullptr;
}
#else
bool handleFocusedTextInput(uint32_t lvKey, const char* utf8)
{
    lv_obj_t* input = focusedTextInput();
    if (!input) {
        return false;
    }

    switch (lvKey) {
        case LV_KEY_BACKSPACE:
            lv_textarea_delete_char(input);
            return true;
        case LV_KEY_DEL:
            lv_textarea_delete_char_forward(input);
            return true;
        case LV_KEY_LEFT:
            lv_textarea_cursor_left(input);
            return true;
        case LV_KEY_RIGHT:
            lv_textarea_cursor_right(input);
            return true;
        case LV_KEY_HOME:
            lv_textarea_set_cursor_pos(input, 0);
            return true;
        case LV_KEY_END:
            lv_textarea_set_cursor_pos(input, LV_TEXTAREA_CURSOR_LAST);
            return true;
        default:
            break;
    }

    if (utf8 && utf8[0] >= 0x20 && utf8[0] < 0x7f && utf8[1] == '\0') {
        lv_textarea_add_text(input, utf8);
    }
    return true;
}
#endif

}  // namespace

CC1101ChatApp::CC1101ChatApp()
    : _chat_vm(_router, _model), _chat_view(_chat_vm), _view_models{&_chat_vm}, _views{&_chat_view}
{
}

CC1101ChatApp::~CC1101ChatApp()
{
    stop();
}

void CC1101ChatApp::start()
{
    if (_started) {
        return;
    }

    spdlog::info("CC1101ChatApp: start");
    _started        = true;
    _quit_requested = false;
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, LV_PART_MAIN);
    setupInputGroup();
    _model.start();
    _route_observer_id = _router.currentPage().observe(this, onRouteChanged);
    setCurrentPage(_router.page());
}

void CC1101ChatApp::stop()
{
    if (!_started) {
        return;
    }

    if (_route_observer_id != 0) {
        _router.currentPage().removeObserver(_route_observer_id);
        _route_observer_id = 0;
    }
    if (_current_view) {
        _current_view->onExit();
        _current_view = nullptr;
    }
    if (_current_vm) {
        _current_vm->onExit();
        _current_vm = nullptr;
    }
    _model.stop();
    if (_input_group) {
#if LV_USE_SDL
        lv_indev_t* inputDevice = lv_indev_get_next(nullptr);
        while (inputDevice) {
            if (lv_indev_get_type(inputDevice) == LV_INDEV_TYPE_KEYPAD) {
                lv_indev_remove_event_cb_with_user_data(inputDevice, onKeyboardEvent, this);
            }
            inputDevice = lv_indev_get_next(inputDevice);
        }
#endif
        lv_group_del(_input_group);
        _input_group = nullptr;
    }
    _started = false;
}

void CC1101ChatApp::onKey(uint32_t key)
{
    if (key == '\x1b' && _router.page() == PageId::Chat && !_chat_vm.modalActive()) {
        spdlog::info("CC1101ChatApp: quit requested");
        _quit_requested = true;
        return;
    }

    if (_current_vm) {
        _current_vm->onKey(key);
    }
}

bool CC1101ChatApp::onLvglKeyState(uint32_t lvKey, const char* utf8, bool pressed)
{
    if (!pressed) {
        return true;
    }

    switch (lvKey) {
        case LV_KEY_ESC:
            onKey('\x1b');
            return true;
        case LV_KEY_ENTER:
            onKey('\r');
            return true;
        default:
            break;
    }

#if !LV_USE_SDL
    if (handleFocusedTextInput(lvKey, utf8)) {
        return true;
    }
#else
    if (textInputFocused()) {
        return true;
    }
#endif

    switch (lvKey) {
        case LV_KEY_BACKSPACE:
            onKey('\b');
            return true;
        case LV_KEY_DEL:
            onKey(0x7f);
            return true;
        case LV_KEY_UP:
            onKey(cc1101_chat_key::Up);
            return true;
        case LV_KEY_DOWN:
            onKey(cc1101_chat_key::Down);
            return true;
        case LV_KEY_LEFT:
        case LV_KEY_PREV:
            onKey(cc1101_chat_key::Left);
            return true;
        case LV_KEY_RIGHT:
        case LV_KEY_NEXT:
            onKey(cc1101_chat_key::Right);
            return true;
        default:
            break;
    }

    if (_router.page() == PageId::Chat && !_chat_vm.modalActive()) {
        if (isTextKey(utf8, 'f')) {
            onKey(cc1101_chat_key::Up);
            return true;
        }
        if (isTextKey(utf8, 'x')) {
            onKey(cc1101_chat_key::Down);
            return true;
        }
        if (isTextKey(utf8, 'z')) {
            onKey(cc1101_chat_key::Left);
            return true;
        }
        if (isTextKey(utf8, 'c')) {
            onKey(cc1101_chat_key::Right);
            return true;
        }
    }

    if (utf8 && utf8[0] >= 0x20 && utf8[0] < 0x7f && utf8[1] == '\0') {
        onKey(static_cast<uint8_t>(utf8[0]));
    }
    return true;
}

void CC1101ChatApp::tick(uint32_t nowMs)
{
    if (_current_vm) {
        _current_vm->tick(nowMs);
    }
    if (_current_view) {
        _current_view->tick(nowMs);
    }
}

ViewModel* CC1101ChatApp::viewModelFor(PageId page)
{
    for (auto* viewModel : _view_models) {
        if (viewModel && viewModel->pageId() == page) {
            return viewModel;
        }
    }
    return nullptr;
}

View* CC1101ChatApp::viewFor(PageId page)
{
    const auto index = static_cast<size_t>(page);
    return index < _views.size() ? _views[index] : nullptr;
}

void CC1101ChatApp::setupInputGroup()
{
    if (_input_group) {
        return;
    }

    _input_group            = lv_group_create();
    lv_indev_t* inputDevice = lv_indev_get_next(nullptr);
    while (inputDevice) {
        if (lv_indev_get_type(inputDevice) == LV_INDEV_TYPE_KEYPAD) {
            lv_indev_set_group(inputDevice, _input_group);
#if LV_USE_SDL
            lv_indev_add_event_cb(inputDevice, onKeyboardEvent, LV_EVENT_KEY, this);
            lv_indev_add_event_cb(inputDevice, onKeyboardEvent, LV_EVENT_RELEASED, this);
#endif
        }
        inputDevice = lv_indev_get_next(inputDevice);
    }
}

void CC1101ChatApp::setCurrentPage(PageId page)
{
    ViewModel* nextViewModel = viewModelFor(page);
    View* nextView           = viewFor(page);
    if (!nextViewModel || !nextView || (nextViewModel == _current_vm && nextView == _current_view)) {
        return;
    }

    if (_current_view) {
        _current_view->onExit();
    }
    if (_current_vm) {
        _current_vm->onExit();
    }

    _current_vm   = nextViewModel;
    _current_view = nextView;
    spdlog::info("Cap-CC1101-SubG-Chat route -> {}", pageIdName(page));
    _current_vm->onEnter();
    _current_view->onEnter(lv_screen_active());
}

void CC1101ChatApp::onRouteChanged(void* context, const PageId& page)
{
    auto* self = static_cast<CC1101ChatApp*>(context);
    if (self) {
        self->setCurrentPage(page);
    }
}

void CC1101ChatApp::onKeyboardEvent(lv_event_t* event)
{
    auto* self        = static_cast<CC1101ChatApp*>(lv_event_get_user_data(event));
    auto* inputDevice = static_cast<lv_indev_t*>(lv_event_get_target(event));
    if (!self || !inputDevice) {
        return;
    }

    const uint32_t key = lv_indev_get_key(inputDevice);
    char utf8[2]       = {0, 0};
    if (key >= 0x20 && key < 0x7f) {
        utf8[0] = static_cast<char>(key);
    }
    const bool pressed =
        lv_event_get_code(event) == LV_EVENT_KEY && lv_indev_get_state(inputDevice) == LV_INDEV_STATE_PRESSED;
    self->onLvglKeyState(key, utf8, pressed);
}

}  // namespace cc1101_chat
