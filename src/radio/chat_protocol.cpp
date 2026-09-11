#include "radio/chat_protocol.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace cc1101_chat::radio::protocol {
namespace {

constexpr std::array<uint8_t, 2> kDataMagic{0xC3, 0xD1};
constexpr std::array<uint8_t, 2> kAcknowledgementMagic{0xC3, 0xA1};
constexpr uint32_t kTokenMask           = 0x00FFFFFFU;
constexpr uint8_t kSenderNameMarker     = 0x80U;
constexpr uint8_t kSenderNameLengthMask = 0x7FU;

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

bool isPrintableAscii(std::string_view value)
{
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return character >= 0x20 && character <= 0x7e;
    });
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

std::vector<uint8_t> encodeData(uint32_t token, const std::vector<uint8_t>& payload, std::string_view sender_name)
{
    if (sender_name.empty()) {
        return encodeData(token, payload);
    }
    if (sender_name.size() > kMaxSenderNameSize || !isPrintableAscii(sender_name)) {
        throw std::invalid_argument("chat protocol sender name must contain 1 to 10 printable ASCII bytes");
    }
    if (payload.size() + sender_name.size() + 1U > kMaxMessageSize) {
        return encodeData(token, payload);
    }

    std::vector<uint8_t> extended_payload;
    extended_payload.reserve(1U + sender_name.size() + payload.size());
    extended_payload.push_back(kSenderNameMarker | static_cast<uint8_t>(sender_name.size()));
    extended_payload.insert(extended_payload.end(), sender_name.begin(), sender_name.end());
    extended_payload.insert(extended_payload.end(), payload.begin(), payload.end());
    return encodeData(token, extended_payload);
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
        return DecodedFrame{FrameKind::Legacy, 0, bytes, {}};
    }
    if (bytes.size() < kHeaderSize) {
        return DecodedFrame{FrameKind::Malformed, 0, {}, {}};
    }

    const uint32_t token = readToken(bytes);
    if (token == 0) {
        return DecodedFrame{FrameKind::Malformed, 0, {}, {}};
    }
    if (acknowledgement) {
        return DecodedFrame{bytes.size() == kHeaderSize ? FrameKind::Acknowledgement : FrameKind::Malformed, token, {},
                            {}};
    }
    if (bytes.size() == kHeaderSize || bytes.size() > kHeaderSize + kMaxMessageSize) {
        return DecodedFrame{FrameKind::Malformed, token, {}, {}};
    }

    auto payload_begin = bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderSize);
    const uint8_t marker                = *payload_begin;
    const std::size_t sender_name_size  = marker & kSenderNameLengthMask;
    const bool has_sender_name = (marker & kSenderNameMarker) != 0 && sender_name_size != 0 &&
                                 sender_name_size <= kMaxSenderNameSize &&
                                 bytes.size() > kHeaderSize + 1U + sender_name_size;
    if (has_sender_name) {
        const auto sender_begin = payload_begin + 1;
        const std::string sender_name(sender_begin, sender_begin + static_cast<std::ptrdiff_t>(sender_name_size));
        if (isPrintableAscii(sender_name)) {
            return DecodedFrame{
                FrameKind::Data, token,
                std::vector<uint8_t>(sender_begin + static_cast<std::ptrdiff_t>(sender_name_size), bytes.end()),
                sender_name};
        }
    }

    return DecodedFrame{FrameKind::Data, token, std::vector<uint8_t>(payload_begin, bytes.end()), {}};
}

}  // namespace cc1101_chat::radio::protocol
