#pragma once

#include <lvgl/lvgl_cpp/label.hpp>
#include <lvgl/lvgl_cpp/obj.hpp>
#include <memory>

namespace cc1101_chat {

class HelpView {
public:
    HelpView(lv_obj_t* parent, lv_group_t* inputGroup);
    ~HelpView();

    void show();
    void hide();
    void toggle();
    void keepFocus();
    bool visible() const;

private:
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _overlay;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _body;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _footer;
    lv_group_t* _input_group = nullptr;
    bool _focused            = false;
};

}  // namespace cc1101_chat
