#include "hal/pinctrl_state.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void requireParses(const std::string& text, int gpio, const char* function, const char* pull, const char* output_level)
{
    cc1101_chat::PinctrlState state;
    std::string error;
    require(cc1101_chat::parsePinctrlState(text, gpio, state, error), error.c_str());
    require(state.gpio == gpio, "GPIO mismatch");
    require(state.function == function, "function mismatch");
    require(state.pull == pull, "pull mismatch");
    require(state.output_level == output_level, "output level mismatch");
}

void requireRejects(const std::string& text, int gpio)
{
    cc1101_chat::PinctrlState state;
    std::string error;
    require(!cc1101_chat::parsePinctrlState(text, gpio, state, error), "malformed state was accepted");
    require(!error.empty(), "parse failure did not provide an error");
}

void requireRestoreArguments(const cc1101_chat::PinctrlState& state, const std::vector<std::string>& expected)
{
    require(cc1101_chat::pinctrlRestoreArguments(state) == expected, "restore arguments mismatch");
}

}  // namespace

int main()
{
    requireParses("14: a5    pn | hi // GPIO14 = SPI0_SIO3\n", 14, "a5", "pn", "");
    requireParses("14: a5    -- | hi // GPIO14 = TXD1\n", 14, "a5", "--", "");
    requireParses("15: ip    pu | lo // GPIO15 = input\n", 15, "ip", "pu", "");
    requireParses("26: op dh pn | hi // GPIO26 = output\n", 26, "op", "pn", "dh");
    requireParses("26: op dl pd | lo // GPIO26 = output\n", 26, "op", "pd", "dl");
    requireParses("26: op -- -- | hi // GPIO26 = output\n", 26, "op", "--", "dh");
    requireParses("26: op -- -- | lo // GPIO26 = output\n", 26, "op", "--", "dl");
    requireParses("26: no    pn | --\n", 26, "no", "pn", "");

    requireRejects("14: a9 pn | hi\n", 14);
    requireRejects("15: gp pn | lo\n", 15);
    requireRejects("14: a5 px | hi\n", 14);
    requireRejects("26: op xx pn | hi\n", 26);
    requireRejects("26: op -- -- | --\n", 26);
    requireRejects("14: a5 pn hi\n", 14);
    requireRejects("15: a5 pn | hi\n", 14);
    requireRejects("14: a5 pn | hi\n14: ip pd | lo\n", 14);
    requireRejects("", 14);

    requireRestoreArguments({14, "a5", "pn", ""}, {"set", "14", "a5", "pn"});
    requireRestoreArguments({14, "a5", "--", ""}, {"set", "14", "a5"});
    requireRestoreArguments({15, "ip", "pu", ""}, {"set", "15", "ip", "pu"});
    requireRestoreArguments({26, "op", "pn", "dh"}, {"set", "26", "op", "dh", "pn"});
    requireRestoreArguments({26, "op", "pd", "dl"}, {"set", "26", "op", "dl", "pd"});
    requireRestoreArguments({26, "op", "--", "dl"}, {"set", "26", "op", "dl"});

    std::cout << "pinctrl state parser tests passed\n";
    return 0;
}
