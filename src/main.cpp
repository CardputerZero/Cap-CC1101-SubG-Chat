#include "core/cc1101_chat_app.hpp"
#include "hal/cc1101_chat_lvgl_hal.hpp"
#include "input/cc1101_chat_keypad.hpp"
#include <core/hal/hal.hpp>
#include <lvgl.h>
#include <spdlog/cfg/env.h>
#include <spdlog/spdlog.h>
#include <csignal>
#include <cstdio>
#include <unistd.h>

namespace {

#if !LV_USE_SDL
constexpr unsigned int kShutdownTimeoutSeconds = 3;
#endif
volatile std::sig_atomic_t g_signal_exit_requested = 0;

void requestExitFromSignal(int signal)
{
    g_signal_exit_requested = signal;
}

void forceExitAfterShutdownTimeout(int)
{
    constexpr char message[] = "Cap-CC1101-SubG-Chat: shutdown timed out; forcing process exit\n";
    const ssize_t ignored    = ::write(STDERR_FILENO, message, sizeof(message) - 1);
    (void)ignored;
    _exit(2);
}

bool installSignalHandlers()
{
    struct sigaction action {};
    action.sa_handler = requestExitFromSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction(SIGINT, &action, nullptr) != 0 || sigaction(SIGTERM, &action, nullptr) != 0) {
        std::perror("Cap-CC1101-SubG-Chat: sigaction");
        return false;
    }

    action.sa_handler = forceExitAfterShutdownTimeout;
    if (sigaction(SIGALRM, &action, nullptr) != 0) {
        std::perror("Cap-CC1101-SubG-Chat: sigaction");
        return false;
    }
    return true;
}

}  // namespace

int main()
{
    constexpr int32_t kScreenWidth  = 320;
    constexpr int32_t kScreenHeight = 170;

    spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%e [%^%l%$] [thread %t] %v");
    spdlog::cfg::load_env_levels();
    if (!installSignalHandlers()) {
        return 1;
    }

    lv_init();
    if (!cc1101_chat::initLvglHal(kScreenWidth, kScreenHeight)) {
        return 1;
    }

    lv_display_t* display = lv_display_get_default();
    if (!display) {
        std::fprintf(stderr, "Cap-CC1101-SubG-Chat: failed to create LVGL display\n");
        cc1101_chat::shutdownLvglHal();
        return 1;
    }

    spdlog::info("Cap-CC1101-SubG-Chat: display {}x{}", static_cast<int>(lv_display_get_horizontal_resolution(display)),
                 static_cast<int>(lv_display_get_vertical_resolution(display)));
    smooth_ui_toolkit::ui_hal::on_get_tick([]() { return lv_tick_get(); });
    smooth_ui_toolkit::ui_hal::on_delay([](uint32_t milliseconds) { usleep(milliseconds * 1000); });

    cc1101_chat::CC1101ChatApp app;

#if !LV_USE_SDL
    cc1101_chat::CC1101ChatKeypad keypad;
    keypad.setKeyCallback(
        [&app](uint32_t key, const char* utf8, bool pressed) { return app.onLvglKeyState(key, utf8, pressed); });
    if (!keypad.openDefault()) {
        spdlog::error("Cap-CC1101-SubG-Chat: no usable keyboard input device; aborting startup");
        keypad.close();
        cc1101_chat::shutdownLvglHal();
        return 1;
    }
#endif

    if (!app.start()) {
#if !LV_USE_SDL
        keypad.close();
#endif
        cc1101_chat::shutdownLvglHal();
        return 1;
    }
    lv_obj_invalidate(lv_screen_active());

    while (!app.quitRequested() && !cc1101_chat::lvglHalQuitRequested() && g_signal_exit_requested == 0) {
#if !LV_USE_SDL
        keypad.poll();
        if (app.quitRequested() || g_signal_exit_requested != 0) {
            break;
        }
#endif
        lv_timer_handler();
        if (cc1101_chat::lvglHalQuitRequested() || g_signal_exit_requested != 0) {
            break;
        }
        app.tick(lv_tick_get());
        usleep(10000);
    }

    spdlog::info("Cap-CC1101-SubG-Chat: exit requested (app={}, display={}, signal={})", app.quitRequested(),
                 cc1101_chat::lvglHalQuitRequested(), static_cast<int>(g_signal_exit_requested));
#if !LV_USE_SDL
    alarm(kShutdownTimeoutSeconds);
#endif
    app.stop();
#if !LV_USE_SDL
    keypad.close();
#endif
    spdlog::info("Cap-CC1101-SubG-Chat: shutting down display HAL");
    cc1101_chat::shutdownLvglHal();
#if !LV_USE_SDL
    alarm(0);
#endif
    spdlog::info("Cap-CC1101-SubG-Chat: shutdown complete");
    return 0;
}
