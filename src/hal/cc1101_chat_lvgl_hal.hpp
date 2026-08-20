#pragma once

#include <lvgl.h>
#include <cstdint>

namespace cc1101_chat {

bool initLvglHal(int32_t width, int32_t height);
bool lvglHalQuitRequested() noexcept;
void shutdownLvglHal();

}  // namespace cc1101_chat
