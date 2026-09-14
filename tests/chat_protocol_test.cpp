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
    require(decodedData.sender_name.empty(), "plain data frame unexpectedly has a sender name");

    const auto namedFrame = encodeData(0x123456, message, "Alice");
    require(namedFrame.size() == kHeaderSize + 1 + 5 + message.size(), "named data frame size is wrong");
    const auto decodedNamed = decode(namedFrame);
    require(decodedNamed.kind == FrameKind::Data, "named data frame kind is wrong");
    require(decodedNamed.token == 0x123456, "named data frame token is wrong");
    require(decodedNamed.payload == message, "named data frame payload is wrong");
    require(decodedNamed.sender_name == "Alice", "named data frame sender is wrong");

    const auto acknowledgement = decode(encodeAcknowledgement(0x654321));
    require(acknowledgement.kind == FrameKind::Acknowledgement, "acknowledgement kind is wrong");
    require(acknowledgement.token == 0x654321, "acknowledgement token is wrong");

    const std::vector<uint8_t> legacy{'o', 'l', 'd'};
    const auto decodedLegacy = decode(legacy);
    require(decodedLegacy.kind == FrameKind::Legacy, "legacy packet was intercepted");
    require(decodedLegacy.payload == legacy, "legacy payload changed");

    std::vector<uint8_t> maximum(kMaxMessageSize, 'x');
    require(encodeData(1, maximum).size() == 61, "maximum message does not fit the physical packet");
    const auto namedMaximum = decode(encodeData(1, maximum, "Alice"));
    require(namedMaximum.payload == maximum, "maximum message changed when sender name did not fit");
    require(namedMaximum.sender_name.empty(), "maximum message did not fall back to the compatible data frame");

    const std::string maximumName(kMaxSenderNameSize, 'n');
    const std::vector<uint8_t> maximumNamedMessage(kMaxMessageSize - 1 - maximumName.size(), 'x');
    require(encodeData(1, maximumNamedMessage, maximumName).size() == 61,
            "maximum named message does not fit the physical packet");
    maximum.push_back('x');
    requireInvalidArgument([&maximum]() { (void)encodeData(1, maximum); }, "oversized message was accepted");
    requireInvalidArgument([&message]() { (void)encodeData(0, message); }, "zero token was accepted");
    requireInvalidArgument([&message]() { (void)encodeData(1, message, "12345678901"); },
                           "oversized sender name was accepted");

    auto malformedAcknowledgement = encodeAcknowledgement(7);
    malformedAcknowledgement.push_back(0);
    require(decode(malformedAcknowledgement).kind == FrameKind::Malformed, "acknowledgement with payload was accepted");
    return 0;
}
