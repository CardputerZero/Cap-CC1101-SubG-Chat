#include "hal/cardputerzero_cc1101_power.hpp"

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

#if defined(CC1101_CHAT_ENABLE_LINUX_RADIO) && CC1101_CHAT_ENABLE_LINUX_RADIO && defined(__linux__)
#include "radio/driver/gpio_line.h"

#include <spdlog/spdlog.h>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <system_error>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;
#endif

namespace cc1101_chat {
namespace {

#if defined(CC1101_CHAT_ENABLE_LINUX_RADIO) && CC1101_CHAT_ENABLE_LINUX_RADIO && defined(__linux__)
constexpr char kLedClassRoot[]         = "/sys/class/leds";
constexpr char kExt5vLedName[]         = "ext_5v_out";
constexpr char kExtUsbGpioFunLedName[] = "ext_usb_gpio_fun";

enum class Ext5vControl { None, LedClass, LegacyGpio };

std::string ledAttributePath(const char* name, const char* attribute)
{
    return std::string(kLedClassRoot) + "/" + name + "/" + attribute;
}

int readLedAttribute(const char* name, const char* attribute)
{
    const std::string path = ledAttributePath(name, attribute);
    const int fd           = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "open " + path);
    }

    char buffer[16]{};
    ssize_t size;
    do {
        size = read(fd, buffer, sizeof(buffer) - 1);
    } while (size < 0 && errno == EINTR);
    const int read_error = errno;
    close(fd);
    if (size < 0) {
        throw std::system_error(read_error, std::generic_category(), "read " + path);
    }
    if (size == 0) {
        throw std::runtime_error("read " + path + " returned no data");
    }

    char* end        = nullptr;
    errno            = 0;
    const long value = std::strtol(buffer, &end, 10);
    if (errno != 0) {
        throw std::system_error(errno, std::generic_category(), "parse " + path);
    }
    if (end == buffer) {
        throw std::runtime_error("invalid integer in " + path);
    }
    return static_cast<int>(value);
}

void writeLedValue(const char* name, bool enabled)
{
    const std::string path = ledAttributePath(name, "brightness");
    const int fd           = open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "open " + path + " for writing");
    }

    const char value = enabled ? '1' : '0';
    ssize_t written;
    do {
        written = write(fd, &value, 1);
    } while (written < 0 && errno == EINTR);
    const int write_error = errno;
    close(fd);
    if (written < 0) {
        throw std::system_error(write_error, std::generic_category(), "write " + path);
    }
    if (written != 1) {
        throw std::runtime_error("short write to " + path);
    }
}

bool ledClassAvailable(const char* name)
{
    const std::string path = ledAttributePath(name, "brightness");
    return access(path.c_str(), F_OK) == 0;
}

void logLedClassInterface(const char* name)
{
    const std::string brightness_path = ledAttributePath(name, "brightness");
    if (access(brightness_path.c_str(), F_OK) != 0) {
        spdlog::info("CC1101 power: LED class interface '{}' is not present", name);
        return;
    }

    try {
        const int brightness     = readLedAttribute(name, "brightness");
        const int max_brightness = readLedAttribute(name, "max_brightness");
        spdlog::info("CC1101 power: LED class interface '{}' detected (brightness={}/{}, writable={})", name,
                     brightness, max_brightness, access(brightness_path.c_str(), W_OK) == 0);
    } catch (const std::exception& exception) {
        spdlog::warn("CC1101 power: LED class interface '{}' is present but unreadable: {}", name, exception.what());
    }
}

std::string pinctrlCommand(const std::vector<std::string>& arguments)
{
    std::string command = "pinctrl";
    for (const auto& argument : arguments) {
        command += " ";
        command += argument;
    }
    return command;
}
#endif

}  // namespace

struct CardputerZeroCc1101Power::Impl {
#if defined(CC1101_CHAT_ENABLE_LINUX_RADIO) && CC1101_CHAT_ENABLE_LINUX_RADIO && defined(__linux__)
    GpioLine gpio26{"/dev/gpiochip0", 26};
    GpioLine legacy_ext5v{"/dev/gpiochip1", 12};
    bool gpio26_requested         = false;
    Ext5vControl ext5v_control    = Ext5vControl::None;
    int previous_ext5v_brightness = 0;
    bool ext5v_restore_needed     = false;
#endif
    bool enabled = false;
};

CardputerZeroCc1101Power::CardputerZeroCc1101Power() : _impl(std::make_unique<Impl>())
{
}

CardputerZeroCc1101Power::~CardputerZeroCc1101Power()
{
    disable();
}

bool CardputerZeroCc1101Power::enable(std::string& error, const std::atomic_bool* cancel)
{
#if defined(CC1101_CHAT_ENABLE_LINUX_RADIO) && CC1101_CHAT_ENABLE_LINUX_RADIO && defined(__linux__)
    if (_impl->enabled) {
        error.clear();
        return true;
    }

    error.clear();
    logLedClassInterface(kExt5vLedName);
    logLedClassInterface(kExtUsbGpioFunLedName);
    spdlog::info("CC1101 power: configuring Cap pins (G14/G15 alternate function, G26 high)");
    if (!runPinctrl({"set", "14", "a5"}, error, cancel) || !runPinctrl({"set", "15", "a5"}, error, cancel) ||
        !runPinctrl({"set", "26", "op", "dh"}, error, cancel)) {
        disable();
        return false;
    }

    try {
        spdlog::debug("CC1101 power: requesting /dev/gpiochip0 line 26 high");
        _impl->gpio26_requested = true;
        _impl->gpio26.setValue(true);

        if (ledClassAvailable(kExt5vLedName)) {
            const std::string brightness_path = ledAttributePath(kExt5vLedName, "brightness");
            _impl->previous_ext5v_brightness  = readLedAttribute(kExt5vLedName, "brightness");
            _impl->ext5v_control              = Ext5vControl::LedClass;
            if (_impl->previous_ext5v_brightness <= 0) {
                spdlog::debug("CC1101 power: enabling EXT5V through {}", brightness_path);
                try {
                    writeLedValue(kExt5vLedName, true);
                    _impl->ext5v_restore_needed = true;
                } catch (const std::system_error& exception) {
                    if (exception.code() == std::make_error_code(std::errc::permission_denied)) {
                        throw std::runtime_error("EXT5V LED class is read-only for this user; grant write access to " +
                                                 brightness_path +
                                                 ", pre-enable it as root, or run this hardware test as root");
                    }
                    throw;
                }
            } else {
                spdlog::info("CC1101 power: EXT5V was already enabled; leaving system-owned state unchanged");
            }

            try {
                const int brightness = readLedAttribute(kExt5vLedName, "brightness");
                if (brightness > 0) {
                    spdlog::info("CC1101 power: EXT5V enabled through LED class (brightness={})", brightness);
                } else {
                    spdlog::warn(
                        "CC1101 power: EXT5V enable write succeeded but LED class still reports brightness=0; "
                        "continuing to the CC1101 hardware probe");
                }
            } catch (const std::exception& exception) {
                spdlog::warn(
                    "CC1101 power: EXT5V enable write succeeded but readback failed: {}; continuing to the "
                    "CC1101 hardware probe",
                    exception.what());
            }
        } else {
            spdlog::warn(
                "CC1101 power: '{}' LED class interface is unavailable; falling back to /dev/gpiochip1 line 12",
                kExt5vLedName);
            _impl->ext5v_control = Ext5vControl::LegacyGpio;
            _impl->legacy_ext5v.setValue(true);
        }

        _impl->enabled = true;
        spdlog::info("CC1101 power: Cap power controls configured (gpiochip0:26=1, EXT5V requested on)");
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        disable();
        return false;
    }
#else
    (void)cancel;
    error = "CardputerZero CC1101 power control is unavailable in this build";
    return false;
#endif
}

void CardputerZeroCc1101Power::disable() noexcept
{
#if defined(CC1101_CHAT_ENABLE_LINUX_RADIO) && CC1101_CHAT_ENABLE_LINUX_RADIO && defined(__linux__)
    if (_impl->ext5v_control == Ext5vControl::LedClass && _impl->ext5v_restore_needed) {
        try {
            writeLedValue(kExt5vLedName, _impl->previous_ext5v_brightness != 0);
        } catch (const std::exception& exception) {
            spdlog::warn("CC1101 power: failed to restore EXT5V LED class state: {}", exception.what());
        }
    } else if (_impl->ext5v_control == Ext5vControl::LegacyGpio) {
        try {
            _impl->legacy_ext5v.setValue(false);
        } catch (...) {
        }
        _impl->legacy_ext5v.unexportLine();
    }
    _impl->ext5v_control        = Ext5vControl::None;
    _impl->ext5v_restore_needed = false;
    if (_impl->gpio26_requested) {
        try {
            _impl->gpio26.setValue(false);
        } catch (...) {
        }
        _impl->gpio26.unexportLine();
        _impl->gpio26_requested = false;
    }
#endif
    _impl->enabled = false;
}

bool CardputerZeroCc1101Power::enabled() const noexcept
{
    return _impl->enabled;
}

bool CardputerZeroCc1101Power::runPinctrl(const std::vector<std::string>& arguments, std::string& error,
                                          const std::atomic_bool* cancel)
{
#if defined(CC1101_CHAT_ENABLE_LINUX_RADIO) && CC1101_CHAT_ENABLE_LINUX_RADIO && defined(__linux__)
    const std::string command = pinctrlCommand(arguments);
    const auto started_at     = std::chrono::steady_clock::now();
    spdlog::debug("CC1101 power: running `{}`", command);

    std::vector<std::string> storage;
    storage.reserve(arguments.size() + 1);
    storage.emplace_back("pinctrl");
    storage.insert(storage.end(), arguments.begin(), arguments.end());

    std::vector<char*> argv;
    argv.reserve(storage.size() + 1);
    for (auto& value : storage) {
        argv.push_back(value.data());
    }
    argv.push_back(nullptr);

    pid_t child            = -1;
    const int spawn_result = posix_spawnp(&child, "pinctrl", nullptr, nullptr, argv.data(), environ);
    if (spawn_result != 0) {
        error = command + " spawn failed: " + std::string(std::strerror(spawn_result));
        return false;
    }

    int status          = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (true) {
        const pid_t wait_result = waitpid(child, &status, WNOHANG);
        if (wait_result == child) {
            break;
        }
        if (wait_result < 0 && errno != EINTR) {
            error = command + " wait failed: " + std::string(std::strerror(errno));
            return false;
        }
        if (cancel && cancel->load(std::memory_order_acquire)) {
            kill(child, SIGKILL);
            (void)waitpid(child, &status, 0);
            error = command + " cancelled";
            return false;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(child, SIGKILL);
            (void)waitpid(child, &status, 0);
            error = command + " timed out after 2000 ms";
            return false;
        }
        if (wait_result < 0 && errno == EINTR) {
            continue;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (WIFSIGNALED(status)) {
            error = command + " terminated by signal " + std::to_string(WTERMSIG(status));
        } else {
            error = command + " exited with status " + std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        }
        return false;
    }
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_at).count();
    spdlog::debug("CC1101 power: `{}` completed in {} ms", command, elapsed);
    return true;
#else
    (void)arguments;
    (void)cancel;
    error = "CardputerZero CC1101 power control is unavailable in this build";
    return false;
#endif
}

}  // namespace cc1101_chat
