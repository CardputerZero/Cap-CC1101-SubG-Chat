#include "hal/pinctrl_state.hpp"

#include <charconv>
#include <sstream>
#include <string>
#include <utility>

namespace cc1101_chat {
namespace {

bool isFunction(const std::string& value)
{
    return value == "ip" || value == "op" || value == "no" ||
           (value.size() == 2 && value[0] == 'a' && value[1] >= '0' && value[1] <= '8');
}

bool isPull(const std::string& value)
{
    return value == "pn" || value == "pu" || value == "pd" || value == "--";
}

bool parseGpioToken(const std::string& token, int& gpio)
{
    if (token.size() < 2 || token.back() != ':') {
        return false;
    }

    const char* begin = token.data();
    const char* end   = token.data() + token.size() - 1;
    const auto result = std::from_chars(begin, end, gpio);
    return result.ec == std::errc{} && result.ptr == end;
}

}  // namespace

bool parsePinctrlState(std::string_view output, int expected_gpio, PinctrlState& state, std::string& error)
{
    std::istringstream lines{std::string(output)};
    std::string line;
    std::string state_line;
    while (std::getline(lines, line)) {
        if (line.find_first_not_of(" \t\r") == std::string::npos) {
            continue;
        }
        if (!state_line.empty()) {
            error = "pinctrl get returned more than one state line";
            return false;
        }
        state_line = line;
    }

    if (state_line.empty()) {
        error = "pinctrl get returned no state";
        return false;
    }

    std::istringstream tokens(state_line);
    std::string gpio_token;
    PinctrlState parsed;
    if (!(tokens >> gpio_token >> parsed.function)) {
        error = "pinctrl state is missing GPIO or function";
        return false;
    }
    if (!parseGpioToken(gpio_token, parsed.gpio)) {
        error = "invalid GPIO token '" + gpio_token + "'";
        return false;
    }
    if (parsed.gpio != expected_gpio) {
        error = "pinctrl returned GPIO" + std::to_string(parsed.gpio) + " while GPIO" + std::to_string(expected_gpio) +
                " was requested";
        return false;
    }
    if (!isFunction(parsed.function)) {
        error = "unsupported GPIO" + std::to_string(parsed.gpio) + " function '" + parsed.function + "'";
        return false;
    }

    bool infer_output_level = false;
    if (parsed.function == "op") {
        if (!(tokens >> parsed.output_level) ||
            (parsed.output_level != "dh" && parsed.output_level != "dl" && parsed.output_level != "--")) {
            error = "GPIO" + std::to_string(parsed.gpio) + " output state is missing dh/dl";
            return false;
        }
        infer_output_level = parsed.output_level == "--";
    }

    if (!(tokens >> parsed.pull) || !isPull(parsed.pull)) {
        error = "GPIO" + std::to_string(parsed.gpio) + " pull state is missing or unsupported";
        return false;
    }

    std::string separator;
    std::string sampled_level;
    if (!(tokens >> separator >> sampled_level) || separator != "|" ||
        (sampled_level != "hi" && sampled_level != "lo" && sampled_level != "--")) {
        error = "GPIO" + std::to_string(parsed.gpio) + " state has an invalid level separator";
        return false;
    }
    if (infer_output_level) {
        if (sampled_level == "--") {
            error = "GPIO" + std::to_string(parsed.gpio) + " output level is unavailable";
            return false;
        }
        parsed.output_level = sampled_level == "hi" ? "dh" : "dl";
    }

    state = std::move(parsed);
    error.clear();
    return true;
}

std::vector<std::string> pinctrlRestoreArguments(const PinctrlState& state)
{
    std::vector<std::string> arguments{"set", std::to_string(state.gpio), state.function};
    if (state.function == "op") {
        arguments.push_back(state.output_level);
    }
    if (state.pull != "--") {
        arguments.push_back(state.pull);
    }
    return arguments;
}

}  // namespace cc1101_chat
