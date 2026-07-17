#include "radio/radio_backend.hpp"

#include <stdexcept>

namespace cc1101_chat::radio {

std::unique_ptr<RadioBackend> makeDefaultRadioBackend()
{
#if defined(CC1101_CHAT_USE_MOCK_RADIO) && CC1101_CHAT_USE_MOCK_RADIO
    return makeMockRadioBackend();
#elif defined(CC1101_CHAT_ENABLE_LINUX_RADIO) && CC1101_CHAT_ENABLE_LINUX_RADIO
    return makeLinuxCc1101Backend();
#else
    throw std::runtime_error(
        "no CC1101 backend selected; define CC1101_CHAT_USE_MOCK_RADIO or CC1101_CHAT_ENABLE_LINUX_RADIO");
#endif
}

}  // namespace cc1101_chat::radio
