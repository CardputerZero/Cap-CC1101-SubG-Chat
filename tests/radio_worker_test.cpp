#include "radio/chat_protocol.hpp"
#include "radio/radio_backend.hpp"
#include "radio/radio_worker.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace {

using namespace cc1101_chat::radio;
using Clock = std::chrono::steady_clock;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct BackendState {
    std::atomic_int data_attempts{0};
};

class RetryBackend final : public RadioBackend {
public:
    explicit RetryBackend(std::shared_ptr<BackendState> state) : _state(std::move(state))
    {
    }

    RadioInfo open(const CancellationToken&) override
    {
        _open = true;
        return RadioInfo{};
    }

    void close() noexcept override
    {
        _open      = false;
        _receiving = false;
        _pending.clear();
    }

    void startReceive(const CancellationToken&) override
    {
        require(_open, "startReceive called while closed");
        _receiving = true;
    }

    void stopReceive() override
    {
        require(_open, "stopReceive called while closed");
        _receiving = false;
    }

    bool receive(RadioPacket& packet, std::chrono::milliseconds timeout, const CancellationToken& cancellation) override
    {
        const auto deadline = Clock::now() + timeout;
        while (Clock::now() < deadline) {
            cancellation.throwIfCancellationRequested();
            if (_receiving && !_pending.empty()) {
                packet.data = std::move(_pending.front());
                _pending.pop_front();
                packet.crc_ok = true;
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }

    void transmit(const std::vector<uint8_t>& payload, const CancellationToken&) override
    {
        require(_open, "transmit called while closed");
        const protocol::DecodedFrame frame = protocol::decode(payload);
        if (frame.kind != protocol::FrameKind::Data) {
            return;
        }
        if (++_state->data_attempts == 2) {
            _pending.push_back(protocol::encodeAcknowledgement(frame.token));
        }
    }

private:
    std::shared_ptr<BackendState> _state;
    std::deque<std::vector<uint8_t>> _pending;
    bool _open      = false;
    bool _receiving = false;
};

}  // namespace

namespace cc1101_chat::radio {

std::unique_ptr<RadioBackend> makeDefaultRadioBackend()
{
    return nullptr;
}

}  // namespace cc1101_chat::radio

int main()
{
    using namespace cc1101_chat::radio;

    auto state = std::make_shared<BackendState>();
    RadioWorker worker(std::make_unique<RetryBackend>(state));
    require(worker.start(true), "worker did not start");

    RadioSendCommand send;
    send.id      = 42;
    send.payload = {'r', 'e', 't', 'r', 'y'};
    require(worker.post(RadioCommand{std::move(send)}) == RadioPostResult::Accepted, "send was rejected");

    bool completed      = false;
    const auto deadline = Clock::now() + std::chrono::seconds(4);
    while (Clock::now() < deadline && !completed) {
        RadioEvent event;
        while (worker.tryPopEvent(event)) {
            if (const auto* tx = std::get_if<RadioTxCompletedEvent>(&event)) {
                completed = tx->id == 42;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    worker.stop();
    require(completed, "message did not complete after retry");
    require(state->data_attempts.load() == 2, "worker did not retry exactly once");
    return 0;
}
