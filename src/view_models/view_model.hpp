#pragma once

#include "core/cc1101_chat_router.hpp"

namespace cc1101_chat {

class ViewModel {
public:
    explicit ViewModel(CC1101ChatRouter& router) : _router(router)
    {
    }
    virtual ~ViewModel() = default;

    ViewModel(const ViewModel&)            = delete;
    ViewModel& operator=(const ViewModel&) = delete;

    virtual PageId pageId() const = 0;
    virtual void onEnter()
    {
    }
    virtual void onExit()
    {
    }
    virtual void onKey(uint32_t key)
    {
        (void)key;
    }
    virtual void onKeyState(uint32_t key, bool pressed)
    {
        (void)key;
        (void)pressed;
    }
    virtual void tick(uint32_t nowMs)
    {
        (void)nowMs;
    }

protected:
    CC1101ChatRouter& _router;
};

}  // namespace cc1101_chat
