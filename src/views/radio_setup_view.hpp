#pragma once

#include "radio/radio_settings.hpp"

#include <array>
#include <functional>
#include <lvgl.h>
#include <string>

namespace cc1101_chat {

class RadioSetupView {
public:
    using ConfirmNotice = std::function<void(bool)>;
    using SaveSettings  = std::function<std::string(radio::RadioSettings)>;

    RadioSetupView(lv_obj_t* parent, ConfirmNotice confirmNotice, SaveSettings saveSettings);
    ~RadioSetupView();

    RadioSetupView(const RadioSetupView&)            = delete;
    RadioSetupView& operator=(const RadioSetupView&) = delete;

    void showNotice();
    void showSettings(radio::RadioSettings settings, bool initial);
    bool visible() const;
    bool onKey(uint32_t key);

private:
    enum class Page { Hidden, Notice, Settings };
    Page _page = Page::Hidden;
    ConfirmNotice _confirm_notice;
    SaveSettings _save_settings;
    radio::RadioSettings _draft;
    std::size_t _field = 0;

    lv_obj_t* _overlay;
    lv_obj_t* _notice;
    lv_obj_t* _settings;
    lv_obj_t* _skip_notice;
    lv_obj_t* _continue;
    lv_obj_t* _cancel;
    lv_obj_t* _save;
    lv_obj_t* _status;
    std::array<lv_obj_t*, 2> _rows{};
    std::array<lv_obj_t*, 2> _values{};
    std::array<std::array<lv_obj_t*, 2>, 2> _arrows{};

    void show(Page page);
    void continueNotice();
    void save();
    void step(std::size_t field, bool forward);
    void render(const char* error = nullptr);
    lv_obj_t* button(lv_obj_t* parent, const char* text, int x, int y, int width, int height, bool accent);
    static void onClick(lv_event_t* event);
};

}  // namespace cc1101_chat
