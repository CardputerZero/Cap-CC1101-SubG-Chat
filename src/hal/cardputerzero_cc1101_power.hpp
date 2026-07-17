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
    void disable() noexcept;
    bool enabled() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;

    static bool runPinctrl(const std::vector<std::string>& arguments, std::string& error,
                           const std::atomic_bool* cancel);
};

}  // namespace cc1101_chat
