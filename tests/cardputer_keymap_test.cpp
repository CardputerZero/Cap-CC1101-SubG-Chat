#include "input/cardputer_keymap.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main()
{
    const cc1101_chat::CardputerKeymap built_in;
    require(!built_in.loadedRuntimeMap(), "empty path unexpectedly loaded a runtime keymap");
    require(built_in.characterFor(26) == '!', "built-in exclamation mapping is wrong");
    require(built_in.characterFor(52) == '*', "built-in asterisk mapping is wrong");
    require(built_in.characterFor(91) == '.', "built-in period mapping is wrong");
    require(built_in.characterFor(8) == 0, "unmapped digit key was intercepted");

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto fixture =
        std::filesystem::temp_directory_path() / ("cc1101-chat-cardputer-keymap-" + std::to_string(nonce) + ".map");
    {
        std::ofstream stream(fixture);
        stream << "# Runtime keycodes are authoritative, even when they overlap normal digit keys.\n"
               << "keycode 8 = period\n"
               << "keycode 6 = exclam\n"
               << "keycode 8 = question\n"
               << "invalid line\n";
    }

    const cc1101_chat::CardputerKeymap runtime(fixture);
    std::error_code error;
    std::filesystem::remove(fixture, error);
    require(runtime.loadedRuntimeMap(), "runtime keymap was not loaded");
    require(runtime.characterFor(8) == '?', "duplicate runtime keycode did not use the latest mapping");
    require(runtime.characterFor(6) == '!', "runtime symbol mapping is wrong");
    require(runtime.characterFor(26) == 0, "runtime keymap did not replace the built-in map");
    return 0;
}
