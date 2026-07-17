#include "core/cc1101_chat_router.hpp"

namespace cc1101_chat {

void CC1101ChatRouter::replace(PageId page)
{
    if (_current_page.get() != page) {
        _current_page.set(page);
    }
}

void CC1101ChatRouter::push(PageId page)
{
    if (_current_page.get() == page) {
        return;
    }
    _history.push_back(_current_page.get());
    _current_page.set(page);
}

void CC1101ChatRouter::back()
{
    if (_history.empty()) {
        replace(PageId::Chat);
        return;
    }

    const PageId previous = _history.back();
    _history.pop_back();
    _current_page.set(previous);
}

}  // namespace cc1101_chat
