#include "views/chat_view.hpp"

#include <core/animation/animate_value/animate_value.hpp>
#include <core/easing/ease.hpp>
#include <lvgl/lvgl_cpp/label.hpp>
#include <lvgl/lvgl_cpp/text_area.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace cc1101_chat {
namespace {

using smooth_ui_toolkit::AnimateValue;
using smooth_ui_toolkit::lvgl_cpp::Container;
using smooth_ui_toolkit::lvgl_cpp::Label;
using smooth_ui_toolkit::lvgl_cpp::TextArea;

constexpr int32_t kScreenWidth                 = 320;
constexpr int32_t kScreenHeight                = 170;
constexpr int32_t kMessageGap                  = 8;
constexpr int32_t kBubbleTailWidth             = 6;
constexpr int32_t kBubbleTailDrop              = 3;
constexpr int32_t kBottomFollowThreshold       = 96;
constexpr float kPageFadeDuration              = 0.15F;
constexpr float kScrollSpringVisualDuration    = 0.40F;
constexpr float kScrollSpringBounce            = 0.0F;
constexpr int32_t kTipShownY                   = -8;
constexpr int32_t kTipHiddenY                  = -29;
constexpr uint32_t kTipHoldMs                  = 3200;
constexpr int32_t kComposeDialogWidth          = 300;
constexpr int32_t kComposeDialogHeight         = 148;
constexpr int32_t kComposeHiddenY              = -170;
constexpr int32_t kComposeOpenWidth            = 180;
constexpr int32_t kComposeOpenHeight           = 120;
constexpr int32_t kInitializationDialogWidth   = 286;
constexpr int32_t kInitializationDialogHeight  = 124;
constexpr int32_t kInitializationDialogHiddenY = -150;
constexpr char kPrintableAscii[] =
    " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";

struct Frame {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
};

lv_opa_t toOpacity(float value)
{
    return static_cast<lv_opa_t>(std::clamp(static_cast<int32_t>(std::lround(value)), 0, 255));
}

class Panel : public Container {
public:
    Panel(lv_obj_t* parent, Frame frame, uint32_t color, lv_opa_t opacity, int32_t radius = 0) : Container(parent)
    {
        setPos(frame.x, frame.y);
        setSize(frame.width, frame.height);
        setBgColor(lv_color_hex(color));
        setBgOpa(opacity);
        setRadius(radius);
        setBorderWidth(0);
        setOutlineWidth(0);
        setShadowWidth(0);
        setPaddingAll(0);
        setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    }
};

class TextLabel : public Label {
public:
    TextLabel(lv_obj_t* parent, std::string_view text, Frame frame, const lv_font_t* font, uint32_t color,
              lv_text_align_t alignment)
        : Label(parent)
    {
        setText(text);
        setLongMode(LV_LABEL_LONG_MODE_WRAP);
        setPos(frame.x, frame.y);
        setSize(frame.width, frame.height);
        setTextFont(font);
        setTextColor(lv_color_hex(color));
        setTextAlign(alignment);
        setBgOpa(LV_OPA_TRANSP);
        setPaddingAll(0);
    }
};

class ActionButton {
public:
    ActionButton(lv_obj_t* parent, Frame frame, std::string_view text, uint32_t background, uint32_t foreground,
                 std::function<void()> action)
        : _button(std::make_unique<Panel>(parent, frame, background, LV_OPA_COVER, 5)),
          _label(std::make_unique<TextLabel>(_button->raw_ptr(), text, Frame{0, 0, frame.width, LV_SIZE_CONTENT},
                                             &lv_font_montserrat_14, foreground, LV_TEXT_ALIGN_CENTER))
    {
        _button->addFlag(LV_OBJ_FLAG_CLICKABLE);
        _button->onClick().connect(std::move(action));
        _label->align(LV_ALIGN_CENTER, 0, 0);
    }

    void align(lv_align_t alignment, int32_t x, int32_t y)
    {
        _button->align(alignment, x, y);
    }

private:
    std::unique_ptr<Panel> _button;
    std::unique_ptr<TextLabel> _label;
};

lv_group_t* keyboardGroup()
{
    lv_indev_t* inputDevice = lv_indev_get_next(nullptr);
    while (inputDevice) {
        if (lv_indev_get_type(inputDevice) == LV_INDEV_TYPE_KEYPAD) {
            lv_group_t* group = lv_indev_get_group(inputDevice);
            if (group) {
                return group;
            }
        }
        inputDevice = lv_indev_get_next(inputDevice);
    }
    return nullptr;
}

std::string messageMetadata(const ChatMessage& message)
{
    char metadata[64] = {};
    if (!message.outgoing) {
        std::snprintf(metadata, sizeof(metadata), "%.0f dBm  /  LQI %u%s", message.rssiDbm,
                      static_cast<unsigned>(message.lqi), message.crcOk ? "" : "  /  CRC!");
    } else if (message.sendFailed) {
        std::snprintf(metadata, sizeof(metadata), "Send failed");
    }
    return metadata;
}

int32_t bubbleWidthFor(std::string_view text, std::string_view metadata)
{
    constexpr int32_t kHorizontalPadding = 10;
    constexpr int32_t kMaxTextWidth      = 224;
    constexpr int32_t kMaxBubbleWidth    = 244;

    lv_point_t textSize{};
    lv_text_get_size(&textSize, text.data(), &lv_font_montserrat_12, 0, 0, kMaxTextWidth, LV_TEXT_FLAG_NONE);
    int32_t contentWidth = textSize.x;
    if (!metadata.empty()) {
        lv_point_t metadataSize{};
        lv_text_get_size(&metadataSize, metadata.data(), &lv_font_montserrat_10, 0, 0, kMaxTextWidth,
                         LV_TEXT_FLAG_NONE);
        contentWidth = std::max(contentWidth, metadataSize.x);
    }

    return std::clamp(contentWidth + kHorizontalPadding * 2, 64, kMaxBubbleWidth);
}

class MessageBubble {
public:
    MessageBubble(lv_obj_t* parent, const ChatMessage& message)
        : _id(message.id),
          _outgoing(message.outgoing),
          _row(std::make_unique<Panel>(parent, Frame{0, 0, LV_PCT(100), LV_SIZE_CONTENT}, 0x000000, LV_OPA_TRANSP))
    {
        const std::string displayText = message.text.empty() ? "<empty>" : message.text;
        const std::string metadata    = messageMetadata(message);
        const int32_t bubbleWidth     = bubbleWidthFor(displayText, metadata);

        _row->setFlexFlow(LV_FLEX_FLOW_ROW);
        _row->setFlexAlign(_outgoing ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_START);
        _row->addEventCb(onDrawTail, LV_EVENT_DRAW_MAIN_END, this);

        const uint32_t bubbleColor = _outgoing ? 0x3FCC75 : 0xCCCCCC;
        _bubble = std::make_unique<Panel>(_row->raw_ptr(), Frame{0, 0, bubbleWidth, LV_SIZE_CONTENT}, bubbleColor,
                                          LV_OPA_COVER, 8);
        lv_obj_set_style_margin_left(_bubble->raw_ptr(), _outgoing ? 0 : kBubbleTailWidth, LV_PART_MAIN);
        lv_obj_set_style_margin_right(_bubble->raw_ptr(), _outgoing ? kBubbleTailWidth : 0, LV_PART_MAIN);
        lv_obj_set_style_margin_bottom(_bubble->raw_ptr(), kBubbleTailDrop, LV_PART_MAIN);
        _bubble->setFlexFlow(LV_FLEX_FLOW_COLUMN);
        _bubble->setFlexAlign(LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        _bubble->setPadding(7, 7, 10, 10);
        _bubble->setPadRow(3);

        _text =
            std::make_unique<TextLabel>(_bubble->raw_ptr(), displayText, Frame{0, 0, bubbleWidth - 20, LV_SIZE_CONTENT},
                                        &lv_font_montserrat_12, 0x000000, LV_TEXT_ALIGN_LEFT);
        if (!metadata.empty()) {
            _metadata = std::make_unique<TextLabel>(
                _bubble->raw_ptr(), metadata, Frame{0, 0, bubbleWidth - 20, LV_SIZE_CONTENT}, &lv_font_montserrat_10,
                message.sendFailed ? 0x9B2C2C : 0x7E7E7E, LV_TEXT_ALIGN_RIGHT);
        }
    }

    uint64_t id() const
    {
        return _id;
    }

    lv_obj_t* raw() const
    {
        return _row->raw_ptr();
    }

private:
    uint64_t _id;
    bool _outgoing;
    std::unique_ptr<Panel> _row;
    std::unique_ptr<Panel> _bubble;
    std::unique_ptr<TextLabel> _text;
    std::unique_ptr<TextLabel> _metadata;

    static void onDrawTail(lv_event_t* event)
    {
        auto* self        = static_cast<MessageBubble*>(lv_event_get_user_data(event));
        lv_layer_t* layer = lv_event_get_layer(event);
        if (!self || !self->_bubble || !layer) {
            return;
        }

        lv_area_t area;
        lv_obj_get_coords(self->_bubble->raw_ptr(), &area);

        lv_draw_triangle_dsc_t descriptor;
        lv_draw_triangle_dsc_init(&descriptor);
        descriptor.color                = lv_obj_get_style_bg_color(self->_bubble->raw_ptr(), LV_PART_MAIN);
        descriptor.opa                  = lv_obj_get_style_bg_opa(self->_bubble->raw_ptr(), LV_PART_MAIN);
        const lv_opa_t recursiveOpacity = lv_obj_get_style_opa_recursive(self->_bubble->raw_ptr(), LV_PART_MAIN);
        if (recursiveOpacity < LV_OPA_MAX) {
            descriptor.opa = LV_OPA_MIX2(descriptor.opa, recursiveOpacity);
        }

        constexpr int32_t kTailRise     = 9;
        constexpr int32_t kTailShoulder = 10;
        const int32_t sideX             = self->_outgoing ? area.x2 : area.x1;
        const int32_t shoulderX         = self->_outgoing ? area.x2 - kTailShoulder : area.x1 + kTailShoulder;
        const int32_t tipX              = self->_outgoing ? area.x2 + kBubbleTailWidth : area.x1 - kBubbleTailWidth;
        descriptor.p[0]                 = {static_cast<lv_value_precise_t>(sideX),
                                           static_cast<lv_value_precise_t>(area.y2 - kTailRise)};
        descriptor.p[1] = {static_cast<lv_value_precise_t>(shoulderX), static_cast<lv_value_precise_t>(area.y2)};
        descriptor.p[2] = {static_cast<lv_value_precise_t>(tipX),
                           static_cast<lv_value_precise_t>(area.y2 + kBubbleTailDrop)};
        lv_draw_triangle(layer, &descriptor);
    }
};

class MessagesTipsHud {
public:
    explicit MessagesTipsHud(lv_obj_t* parent)
        : _panel(std::make_unique<Panel>(parent, Frame{108, kTipHiddenY, 104, 28}, 0x0B0C0E, LV_OPA_COVER, 8)),
          _label(std::make_unique<TextLabel>(_panel->raw_ptr(), "Messages", Frame{0, 8, 104, 18},
                                             &lv_font_montserrat_14, 0xE4E4E4, LV_TEXT_ALIGN_CENTER)),
          _y(kTipHiddenY)
    {
        _panel->setHidden(true);
    }

    void show()
    {
        if (_started) {
            return;
        }

        _started = true;
        _panel->setHidden(false);
        _y.springOptions().visualDuration = 0.4F;
        _y.springOptions().bounce         = 0.22F;
        _y.teleport(kTipHiddenY);
        _y.move(kTipShownY);
    }

    void dismiss()
    {
        if (!_started || _closing) {
            return;
        }

        _closing                          = true;
        _y.springOptions().visualDuration = 0.34F;
        _y.springOptions().bounce         = 0.0F;
        _y.move(kTipHiddenY);
    }

    void tick(uint32_t nowMs)
    {
        if (!_started) {
            return;
        }

        if (_shown_at_ms == 0) {
            _shown_at_ms = nowMs;
        }
        if (!_closing && nowMs - _shown_at_ms >= kTipHoldMs) {
            dismiss();
        }

        _y.update(static_cast<float>(nowMs) / 1000.0F);
        _panel->setY(static_cast<int32_t>(std::lround(_y.directValue())));
        if (_closing && _y.done()) {
            _panel->setHidden(true);
        }
    }

private:
    std::unique_ptr<Panel> _panel;
    std::unique_ptr<TextLabel> _label;
    AnimateValue _y;
    uint32_t _shown_at_ms = 0;
    bool _started         = false;
    bool _closing         = false;
};

class MessageListView {
public:
    MessageListView(lv_obj_t* parent, bool showTitle)
        : _panel(std::make_unique<Panel>(parent, Frame{0, 0, kScreenWidth, kScreenHeight}, 0x000000, LV_OPA_TRANSP)),
          _list(std::make_unique<Panel>(_panel->raw_ptr(), Frame{0, 0, kScreenWidth, kScreenHeight}, 0x000000,
                                        LV_OPA_TRANSP)),
          _empty_label(std::make_unique<TextLabel>(_panel->raw_ptr(), "No messages yet", Frame{0, 60, 320, 16},
                                                   &lv_font_montserrat_12, 0xB2B2B2, LV_TEXT_ALIGN_CENTER)),
          _empty_hint(std::make_unique<TextLabel>(_panel->raw_ptr(), "Type anything to send", Frame{0, 78, 320, 14},
                                                  &lv_font_montserrat_12, 0x5FE492, LV_TEXT_ALIGN_CENTER))
    {
        if (showTitle) {
            _title_hud = std::make_unique<MessagesTipsHud>(_panel->raw_ptr());
        }

        _list->setFlexFlow(LV_FLEX_FLOW_COLUMN);
        _list->setFlexAlign(LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        _list->setPadding(20, 24, 10, 10);
        _list->setPadRow(kMessageGap);
        _list->addFlag(LV_OBJ_FLAG_SCROLLABLE);
        _list->setScrollDir(LV_DIR_VER);
        _list->addEventCb(onListScrolled, LV_EVENT_SCROLL, this);

        auto& scrollSpring          = _scroll_y.springOptions();
        scrollSpring.visualDuration = kScrollSpringVisualDuration;
        scrollSpring.bounce         = kScrollSpringBounce;
        resetScrollAnimation(0.0F);
    }

    ~MessageListView()
    {
        lv_obj_remove_event_cb_with_user_data(_list->raw_ptr(), onListScrolled, this);
    }

    void setMessages(const std::vector<ChatMessage>& messages)
    {
        const size_t first      = messages.size() > kMessageHistoryLimit ? messages.size() - kMessageHistoryLimit : 0;
        const size_t targetSize = messages.size() - first;
        const auto messageAt    = [&](size_t index) -> const ChatMessage& { return messages[first + index]; };

        if (!_synced) {
            rebuild(messages, first);
            _synced = true;
            return;
        }

        if (_rows.size() == targetSize) {
            bool unchanged = true;
            for (size_t index = 0; index < targetSize; ++index) {
                if (_rows[index]->id() != messageAt(index).id) {
                    unchanged = false;
                    break;
                }
            }
            if (unchanged) {
                return;
            }
        }

        lv_obj_update_layout(_list->raw_ptr());
        const bool shouldFollowBottom = _rows.empty() || _follow_bottom;

        if (targetSize == 0) {
            _rows.clear();
            _scroll_animating = false;
            _follow_bottom    = true;
            _scroll_y.teleport(0);
            _list->scrollToY(0, LV_ANIM_OFF);
            updateEmptyState();
            return;
        }

        size_t removeCount = 0;
        while (removeCount < _rows.size() && _rows[removeCount]->id() != messageAt(0).id) {
            ++removeCount;
        }

        bool canUpdateIncrementally = _rows.empty() || removeCount < _rows.size();
        const size_t overlapSize    = _rows.size() - removeCount;
        if (overlapSize > targetSize) {
            canUpdateIncrementally = false;
        }
        if (canUpdateIncrementally) {
            for (size_t index = 0; index < overlapSize; ++index) {
                if (_rows[removeCount + index]->id() != messageAt(index).id) {
                    canUpdateIncrementally = false;
                    break;
                }
            }
        }

        if (!canUpdateIncrementally) {
            rebuild(messages, first);
            return;
        }

        const int32_t oldScrollY = lv_obj_get_scroll_y(_list->raw_ptr());
        int32_t removedExtent    = 0;
        if (!shouldFollowBottom) {
            for (size_t index = 0; index < removeCount; ++index) {
                removedExtent += lv_obj_get_height(_rows[index]->raw()) + kMessageGap;
            }
        }

        for (size_t index = 0; index < removeCount; ++index) {
            _rows.pop_front();
        }

        if (!shouldFollowBottom && removedExtent > 0) {
            lv_obj_update_layout(_list->raw_ptr());
            setScrollImmediate(oldScrollY - removedExtent);
        }

        for (size_t index = _rows.size(); index < targetSize; ++index) {
            _rows.push_back(std::make_unique<MessageBubble>(_list->raw_ptr(), messageAt(index)));
        }

        updateEmptyState();
        lv_obj_update_layout(_list->raw_ptr());
        if (shouldFollowBottom) {
            _follow_bottom = true;
            animateScrollTo(maxScrollY());
        }
    }

    void scrollBy(int32_t amount)
    {
        dismissTitle();
        lv_obj_update_layout(_list->raw_ptr());
        const int32_t maximum = maxScrollY();
        const int32_t base    = _scroll_animating ? static_cast<int32_t>(std::lround(_scroll_y.end))
                                                  : lv_obj_get_scroll_y(_list->raw_ptr());
        const int32_t target  = std::clamp(base - amount, 0, maximum);
        _follow_bottom        = maximum - target <= kBottomFollowThreshold;
        animateScrollTo(target);
    }

    void scrollToBottom()
    {
        _follow_bottom = true;
        animateScrollTo(maxScrollY());
    }

    void setOpacity(lv_opa_t opacity)
    {
        _panel->setOpa(opacity);
    }

    void setHidden(bool hidden)
    {
        _panel->setHidden(hidden);
    }

    void showTitle()
    {
        if (_title_hud) {
            _title_hud->show();
        }
    }

    void dismissTitle()
    {
        if (_title_hud) {
            _title_hud->dismiss();
        }
    }

    void tick(uint32_t nowMs)
    {
        if (_title_hud) {
            _title_hud->tick(nowMs);
        }
        if (!_scroll_animating) {
            return;
        }

        _scroll_y.update(static_cast<float>(nowMs) / 1000.0F);
        _list->scrollToY(static_cast<int32_t>(std::lround(_scroll_y.directValue())), LV_ANIM_OFF);
        if (_scroll_y.done()) {
            setScrollImmediate(static_cast<int32_t>(std::lround(_scroll_y.end)));
        }
    }

private:
    std::unique_ptr<Panel> _panel;
    std::unique_ptr<Panel> _list;
    std::unique_ptr<TextLabel> _empty_label;
    std::unique_ptr<TextLabel> _empty_hint;
    std::unique_ptr<MessagesTipsHud> _title_hud;
    std::deque<std::unique_ptr<MessageBubble>> _rows;
    AnimateValue _scroll_y{0};
    bool _synced           = false;
    bool _scroll_animating = false;
    bool _follow_bottom    = true;

    void rebuild(const std::vector<ChatMessage>& messages, size_t first)
    {
        _rows.clear();
        for (size_t index = first; index < messages.size(); ++index) {
            _rows.push_back(std::make_unique<MessageBubble>(_list->raw_ptr(), messages[index]));
        }

        updateEmptyState();
        lv_obj_update_layout(_list->raw_ptr());
        setScrollImmediate(_rows.empty() ? 0 : maxScrollY());
    }

    void updateEmptyState()
    {
        const bool empty = _rows.empty();
        _empty_label->setHidden(!empty);
        _empty_hint->setHidden(!empty);
    }

    int32_t maxScrollY() const
    {
        return lv_obj_get_scroll_y(_list->raw_ptr()) + lv_obj_get_scroll_bottom(_list->raw_ptr());
    }

    void setScrollImmediate(int32_t target)
    {
        lv_obj_update_layout(_list->raw_ptr());
        target            = std::clamp(target, 0, maxScrollY());
        _scroll_animating = false;
        resetScrollAnimation(static_cast<float>(target));
        _list->scrollToY(target, LV_ANIM_OFF);
        updateFollowState();
    }

    void animateScrollTo(int32_t target)
    {
        lv_obj_update_layout(_list->raw_ptr());
        target                = std::clamp(target, 0, maxScrollY());
        const int32_t current = lv_obj_get_scroll_y(_list->raw_ptr());
        if (!_scroll_animating) {
            resetScrollAnimation(static_cast<float>(current));
            if (target == current) {
                return;
            }
        }
        _scroll_y.move(static_cast<float>(target));
        _scroll_animating = true;
    }

    void resetScrollAnimation(float value)
    {
        _scroll_y.springOptions().velocity = 0.0F;
        _scroll_y.teleport(value);
        _scroll_y.updateWithDelta(0.0F);
    }

    void updateFollowState()
    {
        _follow_bottom = _rows.empty() || lv_obj_get_scroll_bottom(_list->raw_ptr()) <= kBottomFollowThreshold;
    }

    static void onListScrolled(lv_event_t* event)
    {
        auto* self = static_cast<MessageListView*>(lv_event_get_user_data(event));
        if (self && !self->_scroll_animating) {
            self->updateFollowState();
        }
    }
};

class InfoField {
public:
    InfoField(lv_obj_t* parent, std::string_view caption, int32_t x, int32_t width)
        : _caption(std::make_unique<TextLabel>(parent, caption, Frame{x, 54, width, 11}, &lv_font_montserrat_10,
                                               0x777B82, LV_TEXT_ALIGN_LEFT)),
          _value(std::make_unique<TextLabel>(parent, "", Frame{x, 67, width, 17}, &lv_font_montserrat_12, 0xDDE0E4,
                                             LV_TEXT_ALIGN_LEFT))
    {
        _value->setLongMode(LV_LABEL_LONG_MODE_DOTS);
    }

    void setValue(std::string_view value)
    {
        _value->setText(value);
    }

private:
    std::unique_ptr<TextLabel> _caption;
    std::unique_ptr<TextLabel> _value;
};

class RadioInfoView {
public:
    explicit RadioInfoView(lv_obj_t* parent)
        : _panel(std::make_unique<Panel>(parent, Frame{0, 0, kScreenWidth, kScreenHeight}, 0x000000, LV_OPA_TRANSP)),
          _title(std::make_unique<TextLabel>(_panel->raw_ptr(), "CC1101 Info", Frame{0, 0, 320, 18},
                                             &lv_font_montserrat_14, 0xE4E4E4, LV_TEXT_ALIGN_CENTER)),
          _status_dot(
              std::make_unique<Panel>(_panel->raw_ptr(), Frame{0, 0, 6, 6}, 0xC9A45C, LV_OPA_COVER, LV_RADIUS_CIRCLE)),
          _status_label(std::make_unique<TextLabel>(_panel->raw_ptr(), "", Frame{20, 22, 180, 16},
                                                    &lv_font_montserrat_10, 0xAEB2B8, LV_TEXT_ALIGN_LEFT)),
          _chip_label(std::make_unique<TextLabel>(_panel->raw_ptr(), "CC1101", Frame{220, 22, 92, 16},
                                                  &lv_font_montserrat_10, 0x777B82, LV_TEXT_ALIGN_RIGHT)),
          _top_divider(std::make_unique<Panel>(_panel->raw_ptr(), Frame{8, 45, 304, 1}, 0x25272B, LV_OPA_COVER)),
          _device(std::make_unique<InfoField>(_panel->raw_ptr(), "DEVICE", 8, 148)),
          _rssi(std::make_unique<InfoField>(_panel->raw_ptr(), "RSSI", 166, 66)),
          _lqi(std::make_unique<InfoField>(_panel->raw_ptr(), "LQI", 240, 72)),
          _bottom_divider(std::make_unique<Panel>(_panel->raw_ptr(), Frame{8, 89, 304, 1}, 0x25272B, LV_OPA_COVER)),
          _link_caption(std::make_unique<TextLabel>(_panel->raw_ptr(), "LINK", Frame{8, 98, 304, 11},
                                                    &lv_font_montserrat_10, 0x777B82, LV_TEXT_ALIGN_LEFT)),
          _link_value(std::make_unique<TextLabel>(_panel->raw_ptr(), "", Frame{8, 111, 304, 16}, &lv_font_montserrat_12,
                                                  0xDDE0E4, LV_TEXT_ALIGN_LEFT)),
          _stats_value(std::make_unique<TextLabel>(_panel->raw_ptr(), "", Frame{8, 134, 304, 13},
                                                   &lv_font_montserrat_10, 0x777B82, LV_TEXT_ALIGN_LEFT))
    {
        _status_dot->alignTo(*_status_label, LV_ALIGN_OUT_LEFT_MID, -6, 0);
        _link_value->setLongMode(LV_LABEL_LONG_MODE_DOTS);
        _stats_value->setLongMode(LV_LABEL_LONG_MODE_DOTS);
    }

    void setInfo(const ChatRadioInfo& info)
    {
        uint32_t stateColor = 0xC9A45C;
        switch (info.state) {
            case RadioUiState::Receiving:
                stateColor = 0x69AD80;
                break;
            case RadioUiState::Sending:
            case RadioUiState::Initializing:
                stateColor = 0xC9A45C;
                break;
            case RadioUiState::Error:
                stateColor = 0xD96C6C;
                break;
            case RadioUiState::Stopped:
                stateColor = 0x777B82;
                break;
        }

        _status_label->setText(radioUiStateName(info.state));
        _status_dot->setBgColor(lv_color_hex(stateColor));
        _device->setValue(info.spiDevice.empty() ? "Unavailable" : info.spiDevice);

        char value[160] = {};
        if (info.state == RadioUiState::Error) {
            _chip_label->setText("R: RETRY");
        } else if (info.chipVersion.empty() || info.chipVersion == "--") {
            _chip_label->setText("CC1101");
        } else if (info.chipVersion.rfind("CC1101", 0) == 0) {
            _chip_label->setText(info.chipVersion);
        } else {
            std::snprintf(value, sizeof(value), "CC1101 %s", info.chipVersion.c_str());
            _chip_label->setText(value);
        }

        std::snprintf(value, sizeof(value), "%.0f dBm", info.rssiDbm);
        _rssi->setValue(value);
        std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(info.lqi));
        _lqi->setValue(value);

        if (info.link.empty()) {
            std::snprintf(value, sizeof(value), "%.2f MHz", info.frequencyMhz);
            _link_value->setText(value);
        } else {
            _link_value->setText(info.link);
        }

        const bool sendFailed = info.diagnostics.rfind("Send failed:", 0) == 0;
        if ((info.state == RadioUiState::Error || sendFailed) && !info.diagnostics.empty()) {
            _stats_value->setText(info.diagnostics);
        } else {
            std::snprintf(value, sizeof(value), "CRC %s   RX:%llu  TX:%llu  DROP:%llu", info.lastCrcOk ? "OK" : "FAIL",
                          static_cast<unsigned long long>(info.rxCount), static_cast<unsigned long long>(info.txCount),
                          static_cast<unsigned long long>(info.droppedCount));
            _stats_value->setText(value);
        }
        _stats_value->setTextColor(
            lv_color_hex(info.state == RadioUiState::Error || !info.lastCrcOk || sendFailed ? 0xD96C6C : 0x777B82));
    }

    void setOpacity(lv_opa_t opacity)
    {
        _panel->setOpa(opacity);
    }

    void setHidden(bool hidden)
    {
        _panel->setHidden(hidden);
    }

private:
    std::unique_ptr<Panel> _panel;
    std::unique_ptr<TextLabel> _title;
    std::unique_ptr<Panel> _status_dot;
    std::unique_ptr<TextLabel> _status_label;
    std::unique_ptr<TextLabel> _chip_label;
    std::unique_ptr<Panel> _top_divider;
    std::unique_ptr<InfoField> _device;
    std::unique_ptr<InfoField> _rssi;
    std::unique_ptr<InfoField> _lqi;
    std::unique_ptr<Panel> _bottom_divider;
    std::unique_ptr<TextLabel> _link_caption;
    std::unique_ptr<TextLabel> _link_value;
    std::unique_ptr<TextLabel> _stats_value;
};

class PageIndicator {
public:
    explicit PageIndicator(lv_obj_t* parent)
        : _panel(std::make_unique<Panel>(parent, Frame{141, 157, 38, 24}, 0x0B0C0E, LV_OPA_COVER, 7)),
          _messages_dot(
              std::make_unique<Panel>(_panel->raw_ptr(), Frame{11, 3, 5, 5}, 0xE4E4E4, LV_OPA_COVER, LV_RADIUS_CIRCLE)),
          _info_dot(
              std::make_unique<Panel>(_panel->raw_ptr(), Frame{22, 3, 5, 5}, 0x4E5157, LV_OPA_COVER, LV_RADIUS_CIRCLE))
    {
    }

    void setSection(ChatSection section)
    {
        const bool messages = section == ChatSection::Messages;
        _messages_dot->setBgColor(lv_color_hex(messages ? 0xE4E4E4 : 0x4E5157));
        _info_dot->setBgColor(lv_color_hex(messages ? 0x4E5157 : 0xE4E4E4));
    }

    void setHidden(bool hidden)
    {
        _panel->setHidden(hidden);
    }

private:
    std::unique_ptr<Panel> _panel;
    std::unique_ptr<Panel> _messages_dot;
    std::unique_ptr<Panel> _info_dot;
};

class InitializationFailureDialog {
public:
    InitializationFailureDialog(lv_obj_t* parent, ChatViewModel& viewModel)
        : _panel(std::make_unique<Panel>(parent, Frame{0, 0, kInitializationDialogWidth, kInitializationDialogHeight},
                                         0x474747, LV_OPA_COVER, 14)),
          _title(std::make_unique<TextLabel>(_panel->raw_ptr(), "CC1101 Cap unavailable", Frame{0, 0, 260, 18},
                                             &lv_font_montserrat_14, 0xF3F3F3, LV_TEXT_ALIGN_CENTER)),
          _message(std::make_unique<TextLabel>(
              _panel->raw_ptr(), "Connect or reseat the CC1101 Cap,\nthen retry initialization.", Frame{0, 0, 254, 36},
              &lv_font_montserrat_12, 0xBCBCBC, LV_TEXT_ALIGN_CENTER)),
          _close(std::make_unique<ActionButton>(_panel->raw_ptr(), Frame{0, 0, 104, 23}, "ESC: Close", 0x6D6D6D,
                                                0xF3F3F3, [&viewModel]() { viewModel.dismissInitializationDialog(); })),
          _retry(std::make_unique<ActionButton>(_panel->raw_ptr(), Frame{0, 0, 112, 23}, "Enter: Retry", 0xFED40D,
                                                0x5E4D00, [&viewModel]() { viewModel.retryRadio(); })),
          _y(kInitializationDialogHiddenY)
    {
        _title->align(LV_ALIGN_CENTER, 0, -42);
        _message->align(LV_ALIGN_CENTER, 0, -8);
        _close->align(LV_ALIGN_CENTER, -61, 39);
        _retry->align(LV_ALIGN_CENTER, 57, 39);
        applyAnimatedValue();
        _panel->setHidden(true);
    }

    void setActive(bool active)
    {
        if (active == _active) {
            return;
        }

        _active = active;
        if (_active) {
            const bool wasHidden = _hidden;
            _hidden              = false;
            _panel->setHidden(false);
            _panel->moveForeground();
            configureAnimation(0.35F, 0.24F);
            if (wasHidden) {
                _y.teleport(kInitializationDialogHiddenY);
                applyAnimatedValue();
            }
            _y.move(0);
            return;
        }

        configureAnimation(0.28F, 0.0F);
        _y.move(kInitializationDialogHiddenY);
    }

    void tick(uint32_t nowMs)
    {
        _y.update(static_cast<float>(nowMs) / 1000.0F);
        applyAnimatedValue();
        if (!_active && _y.done()) {
            _panel->setHidden(true);
            _hidden = true;
        }
    }

    bool hidden() const
    {
        return _hidden;
    }

private:
    std::unique_ptr<Panel> _panel;
    std::unique_ptr<TextLabel> _title;
    std::unique_ptr<TextLabel> _message;
    std::unique_ptr<ActionButton> _close;
    std::unique_ptr<ActionButton> _retry;
    AnimateValue _y;
    bool _active = false;
    bool _hidden = true;

    void configureAnimation(float duration, float bounce)
    {
        _y.springOptions().visualDuration = duration;
        _y.springOptions().bounce         = bounce;
    }

    void applyAnimatedValue()
    {
        _panel->align(LV_ALIGN_CENTER, 0, static_cast<int32_t>(std::lround(_y.directValue())));
    }
};

class ComposeDialog {
public:
    ComposeDialog(lv_obj_t* parent, ChatViewModel& viewModel)
        : _view_model(viewModel),
          _panel(std::make_unique<Panel>(parent, Frame{0, 0, kComposeOpenWidth, kComposeOpenHeight}, 0x474747,
                                         LV_OPA_COVER, 14)),
          _prompt(std::make_unique<TextLabel>(_panel->raw_ptr(), "New Message", Frame{0, 0, 280, 18},
                                              &lv_font_montserrat_14, 0xA1A1A1, LV_TEXT_ALIGN_LEFT)),
          _input(std::make_unique<TextArea>(_panel->raw_ptr())),
          _status(std::make_unique<TextLabel>(_panel->raw_ptr(), "", Frame{0, 0, 260, 12}, &lv_font_montserrat_10,
                                              0xFED40D, LV_TEXT_ALIGN_RIGHT)),
          _cancel(std::make_unique<ActionButton>(_panel->raw_ptr(), Frame{0, 0, 110, 23}, "ESC: Cancel", 0x6D6D6D,
                                                 0xF3F3F3, [&viewModel]() { viewModel.cancelCompose(); })),
          _send(std::make_unique<ActionButton>(_panel->raw_ptr(), Frame{0, 0, 100, 23}, "Enter: Send", 0xFED40D,
                                               0x5E4D00, [&viewModel]() { viewModel.sendCompose(); })),
          _x(0),
          _y(kComposeHiddenY),
          _width(kComposeOpenWidth),
          _height(kComposeOpenHeight)
    {
        _prompt->align(LV_ALIGN_CENTER, 0, -56);
        setupInput();
        _status->align(LV_ALIGN_CENTER, 0, 16);
        _cancel->align(LV_ALIGN_CENTER, -55, 52);
        _send->align(LV_ALIGN_CENTER, 60, 52);
        configureOpenAnimation();
        applyAnimatedValue();
        _status->setHidden(true);
        _panel->setHidden(true);
    }

    ~ComposeDialog()
    {
        removeInputFromGroup();
    }

    void setDraft(const std::string& draft)
    {
        const char* current = lv_textarea_get_text(_input->raw_ptr());
        if (current && draft == current) {
            return;
        }

        _updating_text = true;
        _input->setText(draft);
        _input->setCursorPos(static_cast<int32_t>(draft.size()));
        _updating_text = false;
    }

    void setStatus(const std::string& status)
    {
        _status->setText(status);
        _status->setHidden(status.empty());
    }

    void setActive(bool active)
    {
        if (active == _active) {
            return;
        }

        _active = active;
        if (_active) {
            _hidden = false;
            _panel->setHidden(false);
            _panel->moveForeground();
            configureOpenAnimation();
            _x.teleport(0);
            _y.teleport(kComposeHiddenY);
            _width.teleport(kComposeOpenWidth);
            _height.teleport(kComposeOpenHeight);
            applyAnimatedValue();
            _x.move(0);
            _y.move(0);
            _width.move(kComposeDialogWidth);
            _height.move(kComposeDialogHeight);
            _focus_pending = true;
            return;
        }

        _focus_pending = false;
        removeInputFromGroup();
        configureCloseAnimation();
        _x.move(0);
        _y.move(kComposeHiddenY);
        _width.move(kComposeOpenWidth);
        _height.move(kComposeOpenHeight);
    }

    void tick(uint32_t nowMs)
    {
        if (_focus_pending) {
            _focus_pending = false;
            focusInput();
        }

        const float now_seconds = static_cast<float>(nowMs) / 1000.0F;
        _x.update(now_seconds);
        _y.update(now_seconds);
        _width.update(now_seconds);
        _height.update(now_seconds);
        applyAnimatedValue();

        if (!_active && _x.done() && _y.done() && _width.done() && _height.done()) {
            _panel->setHidden(true);
            _hidden = true;
        }
    }

    bool hidden() const
    {
        return _hidden;
    }

private:
    ChatViewModel& _view_model;
    std::unique_ptr<Panel> _panel;
    std::unique_ptr<TextLabel> _prompt;
    std::unique_ptr<TextArea> _input;
    std::unique_ptr<TextLabel> _status;
    std::unique_ptr<ActionButton> _cancel;
    std::unique_ptr<ActionButton> _send;
    AnimateValue _x;
    AnimateValue _y;
    AnimateValue _width;
    AnimateValue _height;
    bool _input_in_group = false;
    bool _updating_text  = false;
    bool _focus_pending  = false;
    bool _active         = false;
    bool _hidden         = true;

    static void setupAnimation(AnimateValue& value, float duration, float bounce)
    {
        value.springOptions().visualDuration = duration;
        value.springOptions().bounce         = bounce;
    }

    void configureOpenAnimation()
    {
        setupAnimation(_x, 0.35F, 0.4F);
        setupAnimation(_y, 0.35F, 0.3F);
        setupAnimation(_width, 0.35F, 0.2F);
        setupAnimation(_height, 0.35F, 0.2F);
    }

    void configureCloseAnimation()
    {
        setupAnimation(_x, 0.28F, 0.0F);
        setupAnimation(_y, 0.28F, 0.0F);
        setupAnimation(_width, 0.28F, 0.0F);
        setupAnimation(_height, 0.28F, 0.0F);
    }

    void applyAnimatedValue()
    {
        _panel->setSize(static_cast<int32_t>(std::lround(_width.directValue())),
                        static_cast<int32_t>(std::lround(_height.directValue())));
        _panel->align(LV_ALIGN_CENTER, static_cast<int32_t>(std::lround(_x.directValue())),
                      static_cast<int32_t>(std::lround(_y.directValue())));
    }

    void setupInput()
    {
        _input->setSize(280, 68);
        _input->align(LV_ALIGN_CENTER, 0, -4);
        _input->setBgColor(lv_color_hex(0x555555));
        _input->setBgOpa(LV_OPA_COVER);
        _input->setRadius(8);
        _input->setBorderWidth(0);
        _input->setShadowWidth(0);
        _input->setPadding(8, 18, 10, 10);
        _input->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _input->setTextFont(&lv_font_montserrat_14);
        _input->setTextColor(lv_color_hex(0xFFFFFF));
        _input->setOneLine(false);
        _input->setAcceptedChars(kPrintableAscii);
        _input->setMaxLength(kMaxMessageBytes);
        _input->setOutlineWidth(0, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
        _input->addEventCb(onInputValueChanged, LV_EVENT_VALUE_CHANGED, this);
    }

    void focusInput()
    {
        lv_group_t* group = keyboardGroup();
        if (!group) {
            return;
        }

        if (!_input_in_group) {
            lv_group_add_obj(group, _input->raw_ptr());
            _input_in_group = true;
        }
        lv_group_focus_obj(_input->raw_ptr());
    }

    void removeInputFromGroup()
    {
        if (!_input_in_group) {
            return;
        }

        lv_group_remove_obj(_input->raw_ptr());
        _input_in_group = false;
    }

    static void onInputValueChanged(lv_event_t* event)
    {
        auto* self = static_cast<ComposeDialog*>(lv_event_get_user_data(event));
        if (!self || self->_updating_text) {
            return;
        }

        const char* text = lv_textarea_get_text(self->_input->raw_ptr());
        self->_view_model.setDraft(text ? text : "");
    }
};

}  // namespace

class ChatView::ChatPager {
public:
    ChatPager(lv_obj_t* parent, ChatViewModel& viewModel, bool showTitle)
        : _messages(std::make_unique<MessageListView>(parent, showTitle)),
          _info(std::make_unique<RadioInfoView>(parent)),
          _indicator(std::make_unique<PageIndicator>(parent)),
          _compose_dialog(std::make_unique<ComposeDialog>(parent, viewModel)),
          _initialization_failure_dialog(std::make_unique<InitializationFailureDialog>(parent, viewModel)),
          _messages_opacity(255),
          _info_opacity(0)
    {
        configureFade(_messages_opacity);
        configureFade(_info_opacity);
        _messages->setOpacity(LV_OPA_COVER);
        _messages->setHidden(false);
        _info->setOpacity(LV_OPA_TRANSP);
        _info->setHidden(true);
    }

    void setMessages(const std::vector<ChatMessage>& messages)
    {
        _messages->setMessages(messages);
    }

    void setInfo(const ChatRadioInfo& info)
    {
        _info->setInfo(info);
    }

    void setDraft(const std::string& draft)
    {
        _compose_dialog->setDraft(draft);
    }

    void setComposeStatus(const std::string& status)
    {
        _compose_dialog->setStatus(status);
    }

    void setComposeActive(bool active)
    {
        _compose_active = active;
        if (active) {
            _messages->dismissTitle();
            _indicator->setHidden(true);
        }
        _compose_dialog->setActive(active);
    }

    void setInitializationDialogActive(bool active)
    {
        _initialization_dialog_active = active;
        if (active) {
            _messages->dismissTitle();
            _indicator->setHidden(true);
        }
        _initialization_failure_dialog->setActive(active);
    }

    void setSection(ChatSection section)
    {
        if (!_section_initialized) {
            _section_initialized = true;
            _section             = section;
            _messages_opacity.teleport(section == ChatSection::Messages ? 255.0F : 0.0F);
            _info_opacity.teleport(section == ChatSection::Info ? 255.0F : 0.0F);
            applyOpacity();
            _messages->setHidden(section != ChatSection::Messages);
            _info->setHidden(section != ChatSection::Info);
            _indicator->setSection(section);
            if (section == ChatSection::Messages) {
                _messages->showTitle();
            }
            return;
        }

        if (section == _section) {
            return;
        }

        _messages_opacity.update();
        _info_opacity.update();
        applyOpacity();
        _section = section;
        if (section == ChatSection::Messages) {
            _messages->showTitle();
        } else {
            _messages->dismissTitle();
        }
        _messages->setHidden(false);
        _info->setHidden(false);
        _messages_opacity.move(section == ChatSection::Messages ? 255.0F : 0.0F);
        _info_opacity.move(section == ChatSection::Info ? 255.0F : 0.0F);
        _indicator->setSection(section);
    }

    void scrollMessages(int32_t amount)
    {
        _messages->scrollBy(amount);
    }

    void scrollMessagesToBottom()
    {
        _messages->scrollToBottom();
    }

    void tick(uint32_t nowMs)
    {
        _messages->tick(nowMs);
        const float now_seconds = static_cast<float>(nowMs) / 1000.0F;
        _messages_opacity.update(now_seconds);
        _info_opacity.update(now_seconds);
        applyOpacity();
        _compose_dialog->tick(nowMs);
        _initialization_failure_dialog->tick(nowMs);
        if (!_compose_active && !_initialization_dialog_active && _compose_dialog->hidden() &&
            _initialization_failure_dialog->hidden()) {
            _indicator->setHidden(false);
        }

        if (_section != ChatSection::Messages && _messages_opacity.done() && _messages_opacity.directValue() <= 0.0F) {
            _messages->setHidden(true);
        }
        if (_section != ChatSection::Info && _info_opacity.done() && _info_opacity.directValue() <= 0.0F) {
            _info->setHidden(true);
        }
    }

private:
    std::unique_ptr<MessageListView> _messages;
    std::unique_ptr<RadioInfoView> _info;
    std::unique_ptr<PageIndicator> _indicator;
    std::unique_ptr<ComposeDialog> _compose_dialog;
    std::unique_ptr<InitializationFailureDialog> _initialization_failure_dialog;
    AnimateValue _messages_opacity;
    AnimateValue _info_opacity;
    ChatSection _section               = ChatSection::Messages;
    bool _section_initialized          = false;
    bool _compose_active               = false;
    bool _initialization_dialog_active = false;

    static void configureFade(AnimateValue& opacity)
    {
        opacity.easingOptions().duration       = kPageFadeDuration;
        opacity.easingOptions().easingFunction = smooth_ui_toolkit::ease::ease_in_out_quad;
    }

    void applyOpacity()
    {
        _messages->setOpacity(toOpacity(_messages_opacity.directValue()));
        _info->setOpacity(toOpacity(_info_opacity.directValue()));
    }
};

ChatView::ChatView(ChatViewModel& viewModel) : _view_model(viewModel)
{
}

ChatView::~ChatView()
{
    onExit();
}

void ChatView::onEnter(lv_obj_t* parent)
{
    onExit();

    _root = std::make_unique<Container>(parent);
    _root->setSize(kScreenWidth, kScreenHeight);
    _root->setPos(0, 0);
    _root->setBgColor(lv_color_hex(0x0B0C0E));
    _root->setBgOpa(LV_OPA_COVER);
    _root->setBorderWidth(0);
    _root->setShadowWidth(0);
    _root->setPaddingAll(0);
    _root->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _root->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    const bool show_title = !_messages_tip_shown;
    _messages_tip_shown   = true;
    _pager                = std::make_unique<ChatPager>(_root->raw_ptr(), _view_model, show_title);

    _scroll_serial_seen = _view_model.scrollRequest().get().serial;
    _view_model.messages().observe(this, onMessagesChanged);
    _view_model.radioInfo().observe(this, onRadioInfoChanged);
    _view_model.section().observe(this, onSectionChanged);
    _view_model.scrollRequest().observe(this, onScrollRequestChanged);
    _view_model.draft().observe(this, onDraftChanged);
    _view_model.composeStatus().observe(this, onComposeStatusChanged);
    _view_model.composeActive().observe(this, onComposeActiveChanged);
    _view_model.initializationDialogActive().observe(this, onInitializationDialogActiveChanged);
}

void ChatView::onExit()
{
    _view_model.initializationDialogActive().removeObserver();
    _view_model.composeActive().removeObserver();
    _view_model.composeStatus().removeObserver();
    _view_model.draft().removeObserver();
    _view_model.scrollRequest().removeObserver();
    _view_model.section().removeObserver();
    _view_model.radioInfo().removeObserver();
    _view_model.messages().removeObserver();
    _pager.reset();
    _root.reset();
}

void ChatView::tick(uint32_t nowMs)
{
    if (_pager) {
        _pager->tick(nowMs);
    }
}

void ChatView::onMessagesChanged(void* context, const std::vector<ChatMessage>& messages)
{
    auto* self = static_cast<ChatView*>(context);
    if (self && self->_pager) {
        self->_pager->setMessages(messages);
    }
}

void ChatView::onRadioInfoChanged(void* context, const ChatRadioInfo& info)
{
    auto* self = static_cast<ChatView*>(context);
    if (self && self->_pager) {
        self->_pager->setInfo(info);
    }
}

void ChatView::onSectionChanged(void* context, const ChatSection& section)
{
    auto* self = static_cast<ChatView*>(context);
    if (self && self->_pager) {
        self->_pager->setSection(section);
    }
}

void ChatView::onScrollRequestChanged(void* context, const ChatScrollRequest& request)
{
    auto* self = static_cast<ChatView*>(context);
    if (!self || !self->_pager || request.serial == 0 || request.serial == self->_scroll_serial_seen) {
        return;
    }

    self->_scroll_serial_seen = request.serial;
    if (request.toBottom) {
        self->_pager->scrollMessagesToBottom();
    } else {
        self->_pager->scrollMessages(request.amount);
    }
}

void ChatView::onDraftChanged(void* context, const std::string& draft)
{
    auto* self = static_cast<ChatView*>(context);
    if (self && self->_pager) {
        self->_pager->setDraft(draft);
    }
}

void ChatView::onComposeStatusChanged(void* context, const std::string& status)
{
    auto* self = static_cast<ChatView*>(context);
    if (self && self->_pager) {
        self->_pager->setComposeStatus(status);
    }
}

void ChatView::onComposeActiveChanged(void* context, const bool& active)
{
    auto* self = static_cast<ChatView*>(context);
    if (self && self->_pager) {
        self->_pager->setComposeActive(active);
    }
}

void ChatView::onInitializationDialogActiveChanged(void* context, const bool& active)
{
    auto* self = static_cast<ChatView*>(context);
    if (self && self->_pager) {
        self->_pager->setInitializationDialogActive(active);
    }
}

}  // namespace cc1101_chat
