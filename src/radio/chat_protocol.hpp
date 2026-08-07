#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cc1101_chat::radio::protocol {

inline constexpr std::size_t kHeaderSize     = 5;
inline constexpr std::size_t kMaxMessageSize = 56;

enum class FrameKind {
    Legacy,
    Data,
    Acknowledgement,
    Malformed,
};

struct DecodedFrame {
    FrameKind kind = FrameKind::Legacy;
    uint32_t token = 0;
    std::vector<uint8_t> payload;
};

std::vector<uint8_t> encodeData(uint32_t token, const std::vector<uint8_t>& payload);
std::vector<uint8_t> encodeAcknowledgement(uint32_t token);
DecodedFrame decode(const std::vector<uint8_t>& bytes);

}  // namespace cc1101_chat::radio::protocol
