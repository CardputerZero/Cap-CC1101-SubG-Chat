#include "core/cc1101_chat_types.hpp"

namespace cc1101_chat {

const char* pageIdName(PageId page)
{
    switch (page) {
        case PageId::Chat:
            return "chat";
    }
    return "unknown";
}

const char* radioUiStateName(RadioUiState state)
{
    switch (state) {
        case RadioUiState::Initializing:
            return "INITIALIZING";
        case RadioUiState::Receiving:
            return "RECEIVING";
        case RadioUiState::Sending:
            return "SENDING";
        case RadioUiState::Error:
            return "RADIO OFF";
        case RadioUiState::Stopped:
            return "STOPPED";
    }
    return "UNKNOWN";
}

}  // namespace cc1101_chat
