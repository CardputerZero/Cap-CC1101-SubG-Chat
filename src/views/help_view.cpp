#include "views/help_view.hpp"

namespace cc1101_chat {
namespace {

using smooth_ui_toolkit::lvgl_cpp::Container;
using smooth_ui_toolkit::lvgl_cpp::Label;

void setLabelStyle(Label& label, int32_t x, int32_t y, int32_t width, int32_t height, const lv_font_t* font,
                   uint32_t color, lv_text_align_t alignment = LV_TEXT_ALIGN_LEFT)
{
    label.setPos(x, y);
    label.setSize(width, height);
    label.setLongMode(LV_LABEL_LONG_MODE_WRAP);
    label.setTextFont(font);
    label.setTextColor(lv_color_hex(color));
    label.setTextAlign(alignment);
    label.setBgOpa(LV_OPA_TRANSP);
    label.setPaddingAll(0);
}

}  // namespace

HelpView::HelpView(lv_obj_t* parent, lv_group_t* inputGroup)
    : _overlay(std::make_unique<Container>(parent)),
      _panel(std::make_unique<Container>(_overlay->raw_ptr())),
      _title(std::make_unique<Label>(_panel->raw_ptr())),
      _body(std::make_unique<Label>(_panel->raw_ptr())),
      _footer(std::make_unique<Label>(_panel->raw_ptr())),
      _input_group(inputGroup)
{
    _overlay->setPos(0, 0);
    _overlay->setSize(320, 170);
    _overlay->setBgColor(lv_color_hex(0x000000));
    _overlay->setBgOpa(LV_OPA_80);
    _overlay->setBorderWidth(0);
    _overlay->setOutlineWidth(0, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
    _overlay->setPaddingAll(0);
    _overlay->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _overlay->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _overlay->addFlag(LV_OBJ_FLAG_CLICKABLE);

    _panel->setPos(9, 6);
    _panel->setSize(302, 158);
    _panel->setBgColor(lv_color_hex(0x15171A));
    _panel->setBgOpa(LV_OPA_COVER);
    _panel->setRadius(6);
    _panel->setBorderWidth(1);
    _panel->setBorderColor(lv_color_hex(0x55585E));
    _panel->setPaddingAll(0);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _title->setText("CC1101 Chat");
    setLabelStyle(*_title, 12, 8, 276, 19, &lv_font_montserrat_14, 0xFED40D);
    _body->setText(
        "Connect Cap CC1101 to send and receive messages over Sub-GHz frequencies, with support for group "
        "communication between multiple devices.\n\n"
        "Keyboard: Compose a message\n"
        "F / X / Z / C: Switch between screens");
    setLabelStyle(*_body, 12, 30, 276, 109, &lv_font_montserrat_12, 0xDDE0E4);
    _footer->setText("Fn+H / Esc: close");
    setLabelStyle(*_footer, 12, 141, 276, 14, &lv_font_montserrat_10, 0x9A9FA5, LV_TEXT_ALIGN_RIGHT);

    hide();
}

HelpView::~HelpView() = default;

void HelpView::show()
{
    _overlay->setHidden(false);
    _overlay->moveForeground();
    if (_input_group && !_focused) {
        lv_group_add_obj(_input_group, _overlay->raw_ptr());
        _focused = true;
    }
    keepFocus();
}

void HelpView::hide()
{
    if (_focused) {
        lv_group_remove_obj(_overlay->raw_ptr());
        _focused = false;
    }
    _overlay->setHidden(true);
}

void HelpView::toggle()
{
    if (visible()) {
        hide();
    } else {
        show();
    }
}

void HelpView::keepFocus()
{
    if (_focused && lv_group_get_focused(_input_group) != _overlay->raw_ptr()) {
        lv_group_focus_obj(_overlay->raw_ptr());
    }
}

bool HelpView::visible() const
{
    return !_overlay->hasFlag(LV_OBJ_FLAG_HIDDEN);
}

}  // namespace cc1101_chat
