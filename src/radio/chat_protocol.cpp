#include "radio/chat_protocol.hpp"

#include <array>
#include <stdexcept>

namespace cc1101_chat::radio::protocol {
namespace {

constexpr std::array<uint8_t, 2> kDataMagic{0xC3, 0xD1};
constexpr std::array<uint8_t, 2> kAcknowledgementMagic{0xC3, 0xA1};
constexpr uint32_t kTokenMask = 0x00FFFFFFU;

void appendToken(std::vector<uint8_t>& frame, uint32_t token)
{
    token &= kTokenMask;
    frame.push_back(static_cast<uint8_t>((token >> 16U) & 0xFFU));
    frame.push_back(static_cast<uint8_t>((token >> 8U) & 0xFFU));
    frame.push_back(static_cast<uint8_t>(token & 0xFFU));
}

uint32_t readToken(const std::vector<uint8_t>& frame)
{
    return (static_cast<uint32_t>(frame[2]) << 16U) | (static_cast<uint32_t>(frame[3]) << 8U) |
           static_cast<uint32_t>(frame[4]);
}

bool hasMagic(const std::vector<uint8_t>& frame, const std::array<uint8_t, 2>& magic)
{
    return frame.size() >= magic.size() && frame[0] == magic[0] && frame[1] == magic[1];
}

}  // namespace

std::vector<uint8_t> encodeData(uint32_t token, const std::vector<uint8_t>& payload)
{
    if (token == 0 || (token & ~kTokenMask) != 0) {
        throw std::invalid_argument("chat protocol token must be a non-zero 24-bit value");
    }
    if (payload.empty() || payload.size() > kMaxMessageSize) {
        throw std::invalid_argument("chat protocol payload must contain 1 to 56 bytes");
    }

    std::vector<uint8_t> frame;
    frame.reserve(kHeaderSize + payload.size());
    frame.insert(frame.end(), kDataMagic.begin(), kDataMagic.end());
    appendToken(frame, token);
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

std::vector<uint8_t> encodeAcknowledgement(uint32_t token)
{
    if (token == 0 || (token & ~kTokenMask) != 0) {
        throw std::invalid_argument("chat protocol token must be a non-zero 24-bit value");
    }

    std::vector<uint8_t> frame;
    frame.reserve(kHeaderSize);
    frame.insert(frame.end(), kAcknowledgementMagic.begin(), kAcknowledgementMagic.end());
    appendToken(frame, token);
    return frame;
}

DecodedFrame decode(const std::vector<uint8_t>& bytes)
{
    const bool data            = hasMagic(bytes, kDataMagic);
    const bool acknowledgement = hasMagic(bytes, kAcknowledgementMagic);
    if (!data && !acknowledgement) {
        return DecodedFrame{FrameKind::Legacy, 0, bytes};
    }
    if (bytes.size() < kHeaderSize) {
        return DecodedFrame{FrameKind::Malformed, 0, {}};
    }

    const uint32_t token = readToken(bytes);
    if (token == 0) {
        return DecodedFrame{FrameKind::Malformed, 0, {}};
    }
    if (acknowledgement) {
        return DecodedFrame{bytes.size() == kHeaderSize ? FrameKind::Acknowledgement : FrameKind::Malformed, token, {}};
    }
    if (bytes.size() == kHeaderSize || bytes.size() > kHeaderSize + kMaxMessageSize) {
        return DecodedFrame{FrameKind::Malformed, token, {}};
    }
    return DecodedFrame{FrameKind::Data, token,
                        std::vector<uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderSize), bytes.end())};
}

}  // namespace cc1101_chat::radio::protocol
