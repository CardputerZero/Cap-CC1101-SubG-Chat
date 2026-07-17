#pragma once

#include "radio/radio_backend.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace cc1101_chat::radio {

enum class RadioPostResult {
    Accepted,
    NotRunning,
    QueueFull,
    EmptyPayload,
    PayloadTooLarge,
};

class RadioWorker {
public:
    static constexpr std::size_t kCommandQueueCapacity = 16;
    static constexpr std::size_t kEventQueueCapacity   = 64;

    RadioWorker();
    explicit RadioWorker(std::unique_ptr<RadioBackend> backend);
    ~RadioWorker();

    RadioWorker(const RadioWorker&)            = delete;
    RadioWorker& operator=(const RadioWorker&) = delete;

    bool start(bool receive_on_start = true);
    void stop();
    RadioPostResult post(RadioCommand command);
    bool tryPopEvent(RadioEvent& event);

    bool running() const noexcept
    {
        return _running.load(std::memory_order_acquire);
    }

    std::size_t pendingEventCount() const;

private:
    std::unique_ptr<RadioBackend> _backend;
    std::thread _thread;
    mutable std::mutex _lifecycle_mutex;
    std::atomic_bool _running{false};
    std::atomic_bool _stop_requested{false};
    bool _receive_on_start              = true;
    std::size_t _initialization_attempt = 0;

    mutable std::mutex _command_mutex;
    std::condition_variable _command_cv;
    std::deque<RadioCommand> _commands;

    mutable std::mutex _event_mutex;
    std::deque<RadioEvent> _events;
    std::size_t _pending_dropped_events = 0;

    void run();
    bool initializeBackend(bool receive_requested, const CancellationToken& cancellation);
    bool tryPopCommand(RadioCommand& command);
    void handleCommand(RadioCommand command, bool& initialized, bool& receive_requested,
                       const CancellationToken& cancellation);
    void handleSend(RadioSendCommand command, bool& initialized, bool receive_requested,
                    const CancellationToken& cancellation);
    void pushEvent(RadioEvent event);
    void pushState(RadioState state, std::string detail = {});
};

}  // namespace cc1101_chat::radio
