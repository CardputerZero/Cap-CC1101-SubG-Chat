#include "hal/cc1101_chat_lvgl_hal.hpp"

#include <spdlog/spdlog.h>
#include <cstdlib>

#if LV_USE_SDL
#include "src/drivers/sdl/lv_sdl_keyboard.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#elif LV_USE_LINUX_FBDEV
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#endif

namespace cc1101_chat {
namespace {

const char* envOrDefault(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    return value && value[0] != '\0' ? value : fallback;
}

float envFloatOrDefault(const char* name, float fallback)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return fallback;
    }

    char* end          = nullptr;
    const float parsed = std::strtof(value, &end);
    return end && end != value && parsed > 0.0f ? parsed : fallback;
}

}  // namespace

bool initLvglHal(int32_t width, int32_t height)
{
#if LV_USE_SDL
    lv_display_t* disp = lv_sdl_window_create(width, height);
    if (!disp) {
        spdlog::error("Cap-CC1101-SubG-Chat HAL: failed to create SDL display");
        return false;
    }

    const float zoom = envFloatOrDefault("CC1101_CHAT_SDL_ZOOM", 1.0f);
    lv_sdl_window_set_resizeable(disp, false);
    lv_sdl_window_set_zoom(disp, zoom);
    lv_sdl_window_set_title(disp, envOrDefault("LV_SDL_WINDOW_TITLE", "Cap-CC1101-SubG-Chat"));
    spdlog::info("Cap-CC1101-SubG-Chat HAL: SDL logical display {}x{}, zoom {}", width, height, zoom);
    lv_sdl_mouse_create();
    if (!lv_sdl_keyboard_create()) {
        spdlog::warn("Cap-CC1101-SubG-Chat HAL: failed to create SDL keyboard input");
    }
    return true;
#elif LV_USE_LINUX_FBDEV
    (void)width;
    (void)height;
    lv_display_t* disp = lv_linux_fbdev_create();
    if (!disp) {
        spdlog::error("Cap-CC1101-SubG-Chat HAL: failed to create framebuffer display");
        return false;
    }

    const char* device = envOrDefault("LV_LINUX_FBDEV_DEVICE", "/dev/fb0");
    if (lv_linux_fbdev_set_file(disp, device) != LV_RESULT_OK) {
        spdlog::error("Cap-CC1101-SubG-Chat HAL: failed to open framebuffer {}", device);
        return false;
    }
    return true;
#else
    spdlog::error("Cap-CC1101-SubG-Chat HAL: no LVGL display driver enabled");
    return false;
#endif
}

void shutdownLvglHal()
{
#if LV_USE_SDL
    lv_sdl_quit();
#endif
}

}  // namespace cc1101_chat
