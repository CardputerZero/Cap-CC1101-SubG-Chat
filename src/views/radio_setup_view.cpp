#include "views/radio_setup_view.hpp"

#include "core/cc1101_chat_types.hpp"

#include <cstdio>
#include <utility>

namespace cc1101_chat {
namespace {

constexpr uint32_t kAccent       = 0xFED40D;
constexpr uint32_t kText         = 0xF5F5F5;
constexpr uint32_t kMuted        = 0xB9B9B9;
constexpr std::size_t kFrequency = 0;

lv_obj_t* panel(lv_obj_t* parent, int width, int height, uint32_t color, lv_opa_t opacity, int radius)
{
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, opacity, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* card(lv_obj_t* parent)
{
    auto* obj = panel(parent, 292, 150, 0x141414, LV_OPA_COVER, 10);
    lv_obj_set_style_outline_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_outline_color(obj, lv_color_hex(0x3A3A3A), LV_PART_MAIN);
    lv_obj_center(obj);
    return obj;
}

lv_obj_t* label(lv_obj_t* parent, const char* text, int x, int y, int width, const lv_font_t* font, uint32_t color)
{
    auto* obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), LV_PART_MAIN);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    return obj;
}

}  // namespace

RadioSetupView::RadioSetupView(lv_obj_t* parent, ConfirmNotice confirmNotice, SaveSettings saveSettings)
    : _confirm_notice(std::move(confirmNotice)),
      _save_settings(std::move(saveSettings)),
      _overlay(panel(parent, lv_pct(100), lv_pct(100), 0x000000, LV_OPA_80, 0)),
      _notice(card(_overlay)),
      _settings(card(_overlay))
{
    lv_obj_add_flag(_overlay, LV_OBJ_FLAG_CLICKABLE);
    label(_notice, "Radio regulations", 16, 12, 260, &lv_font_montserrat_18, kAccent);
    label(_notice, "Follow local radio regulations. Use only locally permitted frequencies and transmit power.", 16, 38,
          260, &lv_font_montserrat_14, kText);
    _skip_notice = lv_checkbox_create(_notice);
    lv_checkbox_set_text(_skip_notice, "Don't show again");
    lv_obj_set_pos(_skip_notice, 16, 91);
    lv_obj_set_style_text_font(_skip_notice, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(_skip_notice, lv_color_hex(kText), LV_PART_MAIN);
    lv_obj_set_style_pad_column(_skip_notice, 8, LV_PART_MAIN);
    lv_obj_set_style_text_font(_skip_notice, &lv_font_montserrat_14, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(_skip_notice, 1, LV_PART_INDICATOR);
    lv_obj_set_style_radius(_skip_notice, 3, LV_PART_INDICATOR);
    lv_obj_set_style_border_color(_skip_notice, lv_color_hex(0x707070), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_skip_notice, lv_color_hex(0x202020), LV_PART_INDICATOR);
    constexpr auto checked = static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED;
    lv_obj_set_style_bg_color(_skip_notice, lv_color_hex(kAccent), checked);
    lv_obj_set_style_border_color(_skip_notice, lv_color_hex(kAccent), checked);
    lv_obj_set_style_text_color(_skip_notice, lv_color_black(), checked);
    label(_notice, "7 / Space", 202, 94, 74, &lv_font_montserrat_12, kMuted);
    label(_notice, "Esc: Exit", 16, 119, 132, &lv_font_montserrat_12, kMuted);
    _continue = button(_notice, "Enter: Continue", 151, 112, 125, 26, true);

    label(_settings, "Radio settings", 16, 9, 260, &lv_font_montserrat_18, kAccent);
    constexpr const char* captions[] = {"Frequency (MHz)", "TX power (dBm)"};
    for (std::size_t i = 0; i < _rows.size(); ++i) {
        auto* row = panel(_settings, 260, 24, 0x242424, LV_OPA_COVER, 4);
        _rows[i]  = row;
        lv_obj_set_pos(row, 16, 36 + i * 28);
        lv_obj_set_style_outline_color(row, lv_color_hex(kAccent), LV_PART_MAIN);
        label(row, captions[i], 7, 4, 130, &lv_font_montserrat_12, kText);
        _values[i] = label(row, "", 162, 4, 72, &lv_font_montserrat_12, kText);
        lv_obj_set_style_text_align(_values[i], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        _arrows[i][0] = button(row, LV_SYMBOL_LEFT, 138, 2, 24, 20, false);
        _arrows[i][1] = button(row, LV_SYMBOL_RIGHT, 234, 2, 24, 20, false);
    }
    _status = label(_settings, "", 16, 93, 260, &lv_font_montserrat_12, kMuted);
    _cancel = button(_settings, "Esc: Skip", 16, 112, 114, 26, false);
    _save   = button(_settings, "Enter: Save", 151, 112, 125, 26, true);

    show(Page::Hidden);
}

RadioSetupView::~RadioSetupView()
{
    lv_obj_delete(_overlay);
}

lv_obj_t* RadioSetupView::button(lv_obj_t* parent, const char* text, int x, int y, int width, int height, bool accent)
{
    auto* obj = panel(parent, width, height, accent ? kAccent : 0x363636, LV_OPA_COVER, 5);
    lv_obj_set_pos(obj, x, y);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, onClick, LV_EVENT_CLICKED, this);
    auto* caption = label(obj, text, 0, 0, LV_SIZE_CONTENT, &lv_font_montserrat_14, accent ? 0x141414 : kText);
    lv_obj_center(caption);
    return obj;
}

void RadioSetupView::show(Page page)
{
    _page = page;
    lv_obj_set_flag(_overlay, LV_OBJ_FLAG_HIDDEN, page == Page::Hidden);
    lv_obj_set_flag(_notice, LV_OBJ_FLAG_HIDDEN, page != Page::Notice);
    lv_obj_set_flag(_settings, LV_OBJ_FLAG_HIDDEN, page != Page::Settings);
    if (visible()) lv_obj_move_foreground(_overlay);
}

bool RadioSetupView::visible() const
{
    return _page != Page::Hidden;
}

void RadioSetupView::showNotice()
{
    show(Page::Notice);
}

void RadioSetupView::showSettings(radio::RadioSettings settings, bool initial)
{
    _draft = settings;
    _field = _draft.hasFrequency() && !_draft.hasPower() ? 1 : kFrequency;
    lv_label_set_text(lv_obj_get_child(_cancel, 0), initial ? "Esc: Skip" : "Esc: Cancel");
    render();
    show(Page::Settings);
}

void RadioSetupView::render(const char* error)
{
    char frequency[16] = "Unset";
    if (_draft.hasFrequency()) {
        std::snprintf(frequency, sizeof(frequency), "%d.%03d", _draft.frequencyKhz / 1000, _draft.frequencyKhz % 1000);
    }
    char power[16] = "Unset";
    if (_draft.hasPower()) std::snprintf(power, sizeof(power), "%d", _draft.powerDbm);
    const char* values[]    = {frequency, power};
    const bool configured[] = {_draft.hasFrequency(), _draft.hasPower()};
    for (std::size_t i = 0; i < _rows.size(); ++i) {
        lv_obj_set_style_outline_width(_rows[i], i == _field ? 1 : 0, LV_PART_MAIN);
        lv_obj_set_style_text_color(_values[i], lv_color_hex(configured[i] ? kText : kMuted), LV_PART_MAIN);
        lv_label_set_text(_values[i], values[i]);
    }
    lv_obj_set_style_text_color(_status, lv_color_hex(error ? 0xFF9B91 : kMuted), LV_PART_MAIN);
    lv_label_set_text(_status, error ? error : "F / X: Switch field   Z / C: Change");
}

void RadioSetupView::step(std::size_t field, bool forward)
{
    _field = field;
    if (field == kFrequency)
        _draft.frequencyKhz = radio::stepPreset(radio::kFrequenciesKhz, _draft.frequencyKhz, forward);
    else
        _draft.powerDbm = radio::stepPreset(radio::kPowersDbm, _draft.powerDbm, forward);
    render();
}

void RadioSetupView::continueNotice()
{
    const bool skip = lv_obj_has_state(_skip_notice, LV_STATE_CHECKED);
    show(Page::Hidden);
    _confirm_notice(skip);
}

void RadioSetupView::save()
{
    if (!_draft.complete()) {
        _field = _draft.hasFrequency() ? 1 : kFrequency;
        render("Select both frequency and TX power.");
        return;
    }
    const auto error = _save_settings(_draft);
    if (error.empty())
        show(Page::Hidden);
    else
        render(error.c_str());
}

bool RadioSetupView::onKey(uint32_t key)
{
    if (_page == Page::Notice) {
        if (key == '7' || key == ' ') {
            const bool checked = lv_obj_has_state(_skip_notice, LV_STATE_CHECKED);
            lv_obj_set_state(_skip_notice, LV_STATE_CHECKED, !checked);
        } else if (key == '\r' || key == '8') {
            continueNotice();
        }
        return key != '\x1b';
    }
    if (_page == Page::Settings) {
        switch (key) {
            case '\x1b':
                show(Page::Hidden);
                break;
            case '\r':
                save();
                break;
            case cc1101_chat_key::Up:
            case cc1101_chat_key::Down:
            case 'f':
            case 'F':
            case 'x':
            case 'X':
                _field = 1 - _field;
                render();
                break;
            case cc1101_chat_key::Left:
            case 'z':
            case 'Z':
                step(_field, false);
                break;
            case cc1101_chat_key::Right:
            case 'c':
            case 'C':
                step(_field, true);
                break;
        }
        return true;
    }
    return false;
}

void RadioSetupView::onClick(lv_event_t* event)
{
    auto* self   = static_cast<RadioSetupView*>(lv_event_get_user_data(event));
    auto* target = lv_event_get_current_target_obj(event);
    if (self->_page == Page::Notice && target == self->_continue) {
        self->continueNotice();
    } else if (self->_page == Page::Settings) {
        if (target == self->_save) {
            self->save();
        } else if (target == self->_cancel) {
            self->show(Page::Hidden);
        } else {
            for (std::size_t i = 0; i < self->_arrows.size(); ++i) {
                const auto& arrows = self->_arrows[i];
                if (target == arrows[0] || target == arrows[1]) {
                    self->step(i, target == arrows[1]);
                    return;
                }
            }
        }
    }
}

}  // namespace cc1101_chat
