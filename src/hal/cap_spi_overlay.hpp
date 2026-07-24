#pragma once

#include <atomic>
#include <string>

namespace cc1101_chat {

bool ensureCapSpiOverlay(const std::string& expectedDevice, std::string& error,
                         const std::atomic_bool* cancel = nullptr);

}  // namespace cc1101_chat
