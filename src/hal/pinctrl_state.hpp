#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace cc1101_chat {

struct PinctrlState {
    int gpio = -1;
    std::string function;
    std::string pull;
    std::string output_level;
};

bool parsePinctrlState(std::string_view output, int expected_gpio, PinctrlState& state, std::string& error);
std::vector<std::string> pinctrlRestoreArguments(const PinctrlState& state);

}  // namespace cc1101_chat
