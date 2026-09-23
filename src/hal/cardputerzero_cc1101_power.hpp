#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace cc1101_chat {

class CardputerZeroCc1101Power {
public:
    CardputerZeroCc1101Power();
    ~CardputerZeroCc1101Power();

    CardputerZeroCc1101Power(const CardputerZeroCc1101Power&)            = delete;
    CardputerZeroCc1101Power& operator=(const CardputerZeroCc1101Power&) = delete;

    bool enable(std::string& error, const std::atomic_bool* cancel = nullptr);
    // Unpowered Cap SPI pins load the LCD bus; retain both rails when releasing ownership.
    void disable(bool preserve_spi_power = true) noexcept;
    bool enabled() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;

    static bool runPinctrl(const std::vector<std::string>& arguments, std::string& error,
                           const std::atomic_bool* cancel, int timeout_ms = 2000);
    static bool runPinctrlCapture(const std::vector<std::string>& arguments, std::string& output, std::string& error,
                                  const std::atomic_bool* cancel);
};

}  // namespace cc1101_chat
