#include "radio/chat_protocol.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

template <typename Callable>
void requireInvalidArgument(Callable&& callable, const char* message)
{
    try {
        callable();
    } catch (const std::invalid_argument&) {
        return;
    }
    require(false, message);
}

}  // namespace

int main()
{
    using namespace cc1101_chat::radio::protocol;

    const std::vector<uint8_t> message{'h', 'e', 'l', 'l', 'o'};
    const auto dataFrame = encodeData(0x123456, message);
    require(dataFrame.size() == kHeaderSize + message.size(), "data frame size is wrong");
    const auto decodedData = decode(dataFrame);
    require(decodedData.kind == FrameKind::Data, "data frame kind is wrong");
    require(decodedData.token == 0x123456, "data frame token is wrong");
    require(decodedData.payload == message, "data frame payload is wrong");

    const auto acknowledgement = decode(encodeAcknowledgement(0x654321));
    require(acknowledgement.kind == FrameKind::Acknowledgement, "acknowledgement kind is wrong");
    require(acknowledgement.token == 0x654321, "acknowledgement token is wrong");

    const std::vector<uint8_t> legacy{'o', 'l', 'd'};
    const auto decodedLegacy = decode(legacy);
    require(decodedLegacy.kind == FrameKind::Legacy, "legacy packet was intercepted");
    require(decodedLegacy.payload == legacy, "legacy payload changed");

    std::vector<uint8_t> maximum(kMaxMessageSize, 'x');
    require(encodeData(1, maximum).size() == 61, "maximum message does not fit the physical packet");
    maximum.push_back('x');
    requireInvalidArgument([&maximum]() { (void)encodeData(1, maximum); }, "oversized message was accepted");
    requireInvalidArgument([&message]() { (void)encodeData(0, message); }, "zero token was accepted");

    auto malformedAcknowledgement = encodeAcknowledgement(7);
    malformedAcknowledgement.push_back(0);
    require(decode(malformedAcknowledgement).kind == FrameKind::Malformed, "acknowledgement with payload was accepted");
    return 0;
}
