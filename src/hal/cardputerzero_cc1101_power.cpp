#include "hal/cardputerzero_cc1101_power.hpp"

#include "hal/pinctrl_state.hpp"

#include <array>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

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
constexpr char kLedClassRoot[]          = "/sys/class/leds";
constexpr char kExt5vLedName[]          = "ext_5v_out";
constexpr char kExtUsbGpioFunLedName[]  = "ext_usb_gpio_fun";
constexpr std::array<int, 3> kCapGpios  = {14, 15, 26};
constexpr std::size_t kMaxPinctrlOutput = 16 * 1024;
constexpr int kPinctrlRestoreTimeoutMs  = 250;

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

std::string pinctrlStateDescription(const PinctrlState& state)
{
    std::string description = "function=" + state.function + ", pull=" + state.pull;
    if (state.function == "op") {
        description += ", output=" + state.output_level;
    }
    return description;
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
    std::array<PinctrlState, kCapGpios.size()> previous_pinctrl_states{};
    std::array<bool, kCapGpios.size()> pinctrl_restore_pending{};
    bool pinctrl_restore_needed = false;
#endif
    bool enabled = false;
};

CardputerZeroCc1101Power::CardputerZeroCc1101Power() : _impl(std::make_unique<Impl>())
{
}

CardputerZeroCc1101Power::~CardputerZeroCc1101Power()
{
    disable(true);
}

bool CardputerZeroCc1101Power::enable(std::string& error, const std::atomic_bool* cancel)
{
#if defined(CC1101_CHAT_ENABLE_LINUX_RADIO) && CC1101_CHAT_ENABLE_LINUX_RADIO && defined(__linux__)
    if (_impl->enabled) {
        error.clear();
        return true;
    }

    if (_impl->pinctrl_restore_needed || _impl->ext5v_control != Ext5vControl::None || _impl->gpio26_requested) {
        spdlog::warn("CC1101 power: retrying incomplete cleanup before initialization");
        disable();
        if (_impl->pinctrl_restore_needed || _impl->ext5v_control != Ext5vControl::None || _impl->gpio26_requested) {
            error = "previous CC1101 hardware state could not be restored";
            return false;
        }
    }

    error.clear();
    logLedClassInterface(kExt5vLedName);
    logLedClassInterface(kExtUsbGpioFunLedName);

    std::array<PinctrlState, kCapGpios.size()> pinctrl_snapshot;
    for (std::size_t index = 0; index < kCapGpios.size(); ++index) {
        const int gpio = kCapGpios[index];
        std::string output;
        std::string query_error;
        if (!runPinctrlCapture({"get", std::to_string(gpio)}, output, query_error, cancel)) {
            error = "failed to snapshot GPIO" + std::to_string(gpio) + ": " + query_error;
            spdlog::error("CC1101 power: {}; Cap pins were not modified", error);
            return false;
        }
        if (!parsePinctrlState(output, gpio, pinctrl_snapshot[index], query_error)) {
            error = "failed to parse GPIO" + std::to_string(gpio) + " pinctrl state: " + query_error;
            spdlog::error("CC1101 power: {}; raw output='{}'; Cap pins were not modified", error, output);
            return false;
        }
        spdlog::info("CC1101 power: saved GPIO{} pinctrl state ({})", gpio,
                     pinctrlStateDescription(pinctrl_snapshot[index]));
    }

    _impl->previous_pinctrl_states = std::move(pinctrl_snapshot);
    // A failed command can still have changed a pin, so restoration owns the state before the first set.
    _impl->pinctrl_restore_pending.fill(true);
    _impl->pinctrl_restore_needed = true;
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
                // A failed write can still change the rail, so cleanup owns the prior state before writing.
                _impl->ext5v_restore_needed = true;
                try {
                    writeLedValue(kExt5vLedName, true);
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

void CardputerZeroCc1101Power::disable(bool preserve_ext5v) noexcept
{
#if defined(CC1101_CHAT_ENABLE_LINUX_RADIO) && CC1101_CHAT_ENABLE_LINUX_RADIO && defined(__linux__)
    if (_impl->gpio26_requested) {
        try {
            _impl->gpio26.setValue(false);
        } catch (const std::exception& exception) {
            spdlog::warn("CC1101 power: failed to drive GPIO26 low before release: {}", exception.what());
        } catch (...) {
            spdlog::warn("CC1101 power: failed to drive GPIO26 low before release: unknown error");
        }
    }
    _impl->gpio26.unexportLine();
    _impl->gpio26_requested = false;

    bool ext5v_cleanup_complete = true;
    if (_impl->ext5v_control == Ext5vControl::LedClass && _impl->ext5v_restore_needed) {
        if (preserve_ext5v) {
            spdlog::info("CC1101 power: leaving EXT5V LED class enabled for the next application");
            _impl->ext5v_restore_needed = false;
        } else {
            try {
                writeLedValue(kExt5vLedName, _impl->previous_ext5v_brightness != 0);
                spdlog::info("CC1101 power: restored EXT5V LED class brightness to {}", _impl->previous_ext5v_brightness);
                _impl->ext5v_restore_needed = false;
            } catch (const std::exception& exception) {
                ext5v_cleanup_complete = false;
                spdlog::warn("CC1101 power: failed to restore EXT5V LED class state: {}", exception.what());
            }
        }
    } else if (_impl->ext5v_control == Ext5vControl::LegacyGpio) {
        try {
            _impl->legacy_ext5v.setValue(false);
        } catch (const std::exception& exception) {
            ext5v_cleanup_complete = false;
            spdlog::warn("CC1101 power: failed to drive legacy EXT5V GPIO low: {}", exception.what());
        } catch (...) {
            ext5v_cleanup_complete = false;
            spdlog::warn("CC1101 power: failed to drive legacy EXT5V GPIO low: unknown error");
        }
    }
    _impl->legacy_ext5v.unexportLine();
    if (ext5v_cleanup_complete) {
        _impl->ext5v_control = Ext5vControl::None;
    }

    // The radio object releases G14/G15 before power disable; release our G26 request before restoring pinmux.
    if (_impl->pinctrl_restore_needed) {
        bool restore_pending = false;
        for (std::size_t index = 0; index < _impl->previous_pinctrl_states.size(); ++index) {
            if (!_impl->pinctrl_restore_pending[index]) {
                continue;
            }
            const auto& state = _impl->previous_pinctrl_states[index];
            try {
                std::string restore_error;
                const auto arguments = pinctrlRestoreArguments(state);
                if (runPinctrl(arguments, restore_error, nullptr, kPinctrlRestoreTimeoutMs)) {
                    _impl->pinctrl_restore_pending[index] = false;
                    spdlog::info("CC1101 power: restored GPIO{} pinctrl state ({})", state.gpio,
                                 pinctrlStateDescription(state));
                } else {
                    restore_pending = true;
                    spdlog::warn("CC1101 power: failed to restore GPIO{} pinctrl state ({}): {}", state.gpio,
                                 pinctrlStateDescription(state), restore_error);
                }
            } catch (const std::exception& exception) {
                restore_pending = true;
                spdlog::warn("CC1101 power: failed to restore GPIO{} pinctrl state: {}", state.gpio, exception.what());
            } catch (...) {
                restore_pending = true;
                spdlog::warn("CC1101 power: failed to restore GPIO{} pinctrl state: unknown error", state.gpio);
            }
        }
        _impl->pinctrl_restore_needed = restore_pending;
    }
#endif
    _impl->enabled = false;
}

bool CardputerZeroCc1101Power::enabled() const noexcept
{
    return _impl->enabled;
}

bool CardputerZeroCc1101Power::runPinctrl(const std::vector<std::string>& arguments, std::string& error,
                                          const std::atomic_bool* cancel, int timeout_ms)
{
#if defined(CC1101_CHAT_ENABLE_LINUX_RADIO) && CC1101_CHAT_ENABLE_LINUX_RADIO && defined(__linux__)
    if (timeout_ms <= 0) {
        error = "pinctrl timeout must be positive";
        return false;
    }
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
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        const pid_t wait_result = waitpid(child, &status, WNOHANG);
        if (wait_result == child) {
            break;
        }
        if (wait_result < 0 && errno != EINTR) {
            const int wait_error = errno;
            if (wait_error != ECHILD) {
                (void)kill(child, SIGKILL);
                while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
                }
            }
            error = command + " wait failed: " + std::string(std::strerror(wait_error));
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
            error = command + " timed out after " + std::to_string(timeout_ms) + " ms";
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
    (void)timeout_ms;
    error = "CardputerZero CC1101 power control is unavailable in this build";
    return false;
#endif
}

bool CardputerZeroCc1101Power::runPinctrlCapture(const std::vector<std::string>& arguments, std::string& output,
                                                 std::string& error, const std::atomic_bool* cancel)
{
#if defined(CC1101_CHAT_ENABLE_LINUX_RADIO) && CC1101_CHAT_ENABLE_LINUX_RADIO && defined(__linux__)
    const std::string command = pinctrlCommand(arguments);
    const auto started_at     = std::chrono::steady_clock::now();
    output.clear();
    spdlog::debug("CC1101 power: running `{}` and capturing output", command);

    int pipe_fds[2] = {-1, -1};
    if (pipe(pipe_fds) < 0) {
        error = command + " pipe failed: " + std::string(std::strerror(errno));
        return false;
    }
    for (const int fd : pipe_fds) {
        const int descriptor_flags = fcntl(fd, F_GETFD);
        if (descriptor_flags < 0 || fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) {
            const int descriptor_error = errno;
            close(pipe_fds[0]);
            close(pipe_fds[1]);
            error = command + " pipe setup failed: " + std::string(std::strerror(descriptor_error));
            return false;
        }
    }

    posix_spawn_file_actions_t file_actions;
    int action_result = posix_spawn_file_actions_init(&file_actions);
    if (action_result != 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        error = command + " spawn action setup failed: " + std::string(std::strerror(action_result));
        return false;
    }

    action_result = posix_spawn_file_actions_adddup2(&file_actions, pipe_fds[1], STDOUT_FILENO);
    if (action_result == 0 && pipe_fds[0] != STDOUT_FILENO) {
        action_result = posix_spawn_file_actions_addclose(&file_actions, pipe_fds[0]);
    }
    if (action_result == 0 && pipe_fds[1] != STDOUT_FILENO) {
        action_result = posix_spawn_file_actions_addclose(&file_actions, pipe_fds[1]);
    }
    if (action_result != 0) {
        (void)posix_spawn_file_actions_destroy(&file_actions);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        error = command + " spawn action setup failed: " + std::string(std::strerror(action_result));
        return false;
    }

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
    const int spawn_result = posix_spawnp(&child, "pinctrl", &file_actions, nullptr, argv.data(), environ);
    (void)posix_spawn_file_actions_destroy(&file_actions);
    close(pipe_fds[1]);
    if (spawn_result != 0) {
        close(pipe_fds[0]);
        error = command + " spawn failed: " + std::string(std::strerror(spawn_result));
        return false;
    }

    const int read_flags = fcntl(pipe_fds[0], F_GETFL);
    if (read_flags < 0 || fcntl(pipe_fds[0], F_SETFL, read_flags | O_NONBLOCK) < 0) {
        const int descriptor_error = errno;
        kill(child, SIGKILL);
        (void)waitpid(child, nullptr, 0);
        close(pipe_fds[0]);
        error = command + " capture setup failed: " + std::string(std::strerror(descriptor_error));
        return false;
    }

    bool child_exited          = false;
    bool output_closed         = false;
    int status                 = 0;
    const auto deadline        = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    const auto terminate_child = [&]() {
        if (!child_exited) {
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
            child_exited = true;
        }
    };

    while (!child_exited || !output_closed) {
        char buffer[512];
        while (!output_closed) {
            const ssize_t size = read(pipe_fds[0], buffer, sizeof(buffer));
            if (size > 0) {
                if (output.size() + static_cast<std::size_t>(size) > kMaxPinctrlOutput) {
                    terminate_child();
                    close(pipe_fds[0]);
                    error = command + " returned more than " + std::to_string(kMaxPinctrlOutput) + " bytes";
                    return false;
                }
                output.append(buffer, static_cast<std::size_t>(size));
                continue;
            }
            if (size == 0) {
                output_closed = true;
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            const int read_error = errno;
            terminate_child();
            close(pipe_fds[0]);
            error = command + " capture read failed: " + std::string(std::strerror(read_error));
            return false;
        }

        if (!child_exited) {
            const pid_t wait_result = waitpid(child, &status, WNOHANG);
            if (wait_result == child) {
                child_exited = true;
            } else if (wait_result < 0 && errno != EINTR) {
                const int wait_error = errno;
                if (wait_error == ECHILD) {
                    child_exited = true;
                } else {
                    terminate_child();
                }
                close(pipe_fds[0]);
                error = command + " wait failed: " + std::string(std::strerror(wait_error));
                return false;
            }
        }

        if (cancel && cancel->load(std::memory_order_acquire)) {
            terminate_child();
            close(pipe_fds[0]);
            error = command + " cancelled";
            return false;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            terminate_child();
            close(pipe_fds[0]);
            error = command + " timed out after 2000 ms";
            return false;
        }
        if (!child_exited || !output_closed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    close(pipe_fds[0]);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (WIFSIGNALED(status)) {
            error = command + " terminated by signal " + std::to_string(WTERMSIG(status));
        } else {
            error = command + " exited with status " + std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        }
        return false;
    }
    if (output.empty()) {
        error = command + " returned no output";
        return false;
    }

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_at).count();
    spdlog::debug("CC1101 power: `{}` completed in {} ms with {} output bytes", command, elapsed, output.size());
    error.clear();
    return true;
#else
    (void)arguments;
    (void)cancel;
    output.clear();
    error = "CardputerZero CC1101 power control is unavailable in this build";
    return false;
#endif
}

}  // namespace cc1101_chat
