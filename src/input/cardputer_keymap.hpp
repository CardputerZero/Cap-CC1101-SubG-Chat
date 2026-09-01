#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

namespace cc1101_chat {

class CardputerKeymap {
public:
    explicit CardputerKeymap(const std::filesystem::path& runtime_path = {});

    uint32_t characterFor(uint16_t keycode) const noexcept;
    bool loadedRuntimeMap() const noexcept;
    std::size_t size() const noexcept;

private:
    std::vector<std::pair<uint16_t, char>> _entries;
    bool _loaded_runtime_map = false;
};

}  // namespace cc1101_chat
