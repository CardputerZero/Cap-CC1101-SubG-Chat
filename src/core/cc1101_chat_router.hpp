#pragma once

#include "core/cc1101_chat_types.hpp"
#include <tools/observable/observable.hpp>
#include <vector>

namespace cc1101_chat {

class CC1101ChatRouter {
public:
    smooth_ui_toolkit::Observable<PageId>& currentPage()
    {
        return _current_page;
    }

    PageId page() const
    {
        return _current_page.get();
    }

    void replace(PageId page);
    void push(PageId page);
    void back();

private:
    smooth_ui_toolkit::Observable<PageId> _current_page{PageId::Chat};
    std::vector<PageId> _history;
};

}  // namespace cc1101_chat
