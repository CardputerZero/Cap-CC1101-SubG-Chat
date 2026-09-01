#include "input/cardputer_keymap.hpp"

#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace cc1101_chat {
namespace {

constexpr std::array<std::pair<uint16_t, char>, 32> kBuiltInKeymap{{
    {26, '!'}, {27, '@'}, {39, '#'},  {40, '$'}, {41, '%'}, {43, '^'}, {51, '&'},  {52, '*'},
    {53, '('}, {94, ')'}, {55, '~'},  {69, '`'}, {70, '_'}, {71, '-'}, {72, '+'},  {73, '='},
    {74, '['}, {75, ']'}, {76, '{'},  {77, '}'}, {79, ';'}, {80, ':'}, {81, '\''}, {82, '"'},
    {83, '<'}, {85, '>'}, {86, '\\'}, {89, '|'}, {90, ','}, {91, '.'}, {92, '/'},  {93, '?'},
}};

std::string trim(std::string text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

char characterForSymbol(std::string_view symbol)
{
    constexpr std::array<std::pair<std::string_view, char>, 30> symbols{{
        {"exclam", '!'},      {"at", '@'},          {"numbersign", '#'},   {"dollar", '$'},    {"percent", '%'},
        {"asciicircum", '^'}, {"ampersand", '&'},   {"asterisk", '*'},     {"parenleft", '('}, {"parenright", ')'},
        {"asciitilde", '~'},  {"grave", '`'},       {"underscore", '_'},   {"minus", '-'},     {"plus", '+'},
        {"equal", '='},       {"bracketleft", '['}, {"bracketright", ']'}, {"braceleft", '{'}, {"braceright", '}'},
        {"semicolon", ';'},   {"colon", ':'},       {"apostrophe", '\''},  {"quotedbl", '"'},  {"less", '<'},
        {"greater", '>'},     {"backslash", '\\'},  {"bar", '|'},          {"comma", ','},     {"period", '.'},
    }};
    for (const auto& entry : symbols) {
        if (entry.first == symbol) {
            return entry.second;
        }
    }
    if (symbol == "slash") {
        return '/';
    }
    if (symbol == "question") {
        return '?';
    }
    return 0;
}

void upsert(std::vector<std::pair<uint16_t, char>>& entries, uint16_t keycode, char character)
{
    for (auto& entry : entries) {
        if (entry.first == keycode) {
            entry.second = character;
            return;
        }
    }
    entries.emplace_back(keycode, character);
}

std::vector<std::pair<uint16_t, char>> loadRuntimeMap(const std::filesystem::path& path)
{
    std::ifstream stream(path);
    if (!stream) {
        return {};
    }

    std::vector<std::pair<uint16_t, char>> entries;
    std::string line;
    while (std::getline(stream, line)) {
        if (const auto comment = line.find('#'); comment != std::string::npos) {
            line.erase(comment);
        }
        line = trim(std::move(line));
        if (line.empty()) {
            continue;
        }

        std::istringstream parser(line);
        std::string keyword;
        unsigned int keycode = 0;
        char equals          = 0;
        std::string symbol;
        if (!(parser >> keyword >> keycode >> equals >> symbol) || keyword != "keycode" || equals != '=' ||
            keycode > UINT16_MAX) {
            continue;
        }
        if (const char character = characterForSymbol(symbol)) {
            upsert(entries, static_cast<uint16_t>(keycode), character);
        }
    }
    return entries;
}

}  // namespace

CardputerKeymap::CardputerKeymap(const std::filesystem::path& runtime_path)
    : _entries(kBuiltInKeymap.begin(), kBuiltInKeymap.end())
{
    if (runtime_path.empty()) {
        return;
    }
    auto runtime_entries = loadRuntimeMap(runtime_path);
    if (!runtime_entries.empty()) {
        _entries            = std::move(runtime_entries);
        _loaded_runtime_map = true;
    }
}

uint32_t CardputerKeymap::characterFor(uint16_t keycode) const noexcept
{
    for (const auto& entry : _entries) {
        if (entry.first == keycode) {
            return static_cast<unsigned char>(entry.second);
        }
    }
    return 0;
}

bool CardputerKeymap::loadedRuntimeMap() const noexcept
{
    return _loaded_runtime_map;
}

std::size_t CardputerKeymap::size() const noexcept
{
    return _entries.size();
}

}  // namespace cc1101_chat
