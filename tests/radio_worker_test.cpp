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

struct ReceiverState {
    std::atomic_int acknowledgement_count{0};
};

struct FailureRecoveryState {
    std::atomic_int data_attempts{0};
    std::atomic_int receive_starts{0};
    std::atomic_bool link_available{false};
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

class ReceiverBackend final : public RadioBackend {
public:
    ReceiverBackend(std::shared_ptr<ReceiverState> state, std::vector<uint8_t> data)
        : _state(std::move(state)), _data(std::move(data))
    {
    }

    RadioInfo open(const CancellationToken&) override
    {
        _open = true;
        _pending.push_back(_data);
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
        require(_open, "receiver startReceive called while closed");
        _receiving = true;
    }

    void stopReceive() override
    {
        require(_open, "receiver stopReceive called while closed");
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
        require(_open, "receiver transmit called while closed");
        const protocol::DecodedFrame frame = protocol::decode(payload);
        require(frame.kind == protocol::FrameKind::Acknowledgement, "receiver transmitted a non-ACK frame");
        require(frame.token == 0x102030, "receiver ACK token changed");
        const int count = ++_state->acknowledgement_count;
        if (count == 1) {
            // Model a sender retrying after missing the first ACK frame.
            _pending.push_back(_data);
        }
    }

private:
    std::shared_ptr<ReceiverState> _state;
    std::vector<uint8_t> _data;
    std::deque<std::vector<uint8_t>> _pending;
    bool _open      = false;
    bool _receiving = false;
};

class FailureRecoveryBackend final : public RadioBackend {
public:
    explicit FailureRecoveryBackend(std::shared_ptr<FailureRecoveryState> state) : _state(std::move(state))
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
        require(_open, "failure recovery startReceive called while closed");
        _receiving = true;
        ++_state->receive_starts;
    }

    void stopReceive() override
    {
        require(_open, "failure recovery stopReceive called while closed");
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
        require(_open, "failure recovery transmit called while closed");
        const auto frame = protocol::decode(payload);
        require(frame.kind == protocol::FrameKind::Data, "failure recovery backend received a non-data frame");
        ++_state->data_attempts;
        if (_state->link_available.load()) {
            _pending.push_back(protocol::encodeAcknowledgement(frame.token));
        }
    }

private:
    std::shared_ptr<FailureRecoveryState> _state;
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

    auto receiver_state      = std::make_shared<ReceiverState>();
    const auto receiver_data = protocol::encodeData(0x102030, {'i', 'n', 'b', 'o', 'x'});
    RadioWorker receiver(std::make_unique<ReceiverBackend>(receiver_state, receiver_data));
    require(receiver.start(true), "receiver worker did not start");

    std::size_t received_messages = 0;
    RadioEvent event;
    const auto receiver_deadline = Clock::now() + std::chrono::seconds(2);
    while (Clock::now() < receiver_deadline && receiver_state->acknowledgement_count.load() < 2) {
        while (receiver.tryPopEvent(event)) {
            if (std::holds_alternative<RadioRxPacketEvent>(event)) {
                ++received_messages;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    receiver.stop();
    while (receiver.tryPopEvent(event)) {
        if (std::holds_alternative<RadioRxPacketEvent>(event)) {
            ++received_messages;
        }
    }
    require(receiver_state->acknowledgement_count.load() == 2,
            "receiver did not ACK the original and duplicate packet");
    require(received_messages == 1, "duplicate retransmission was shown as a second message");

    auto failure_state = std::make_shared<FailureRecoveryState>();
    RadioWorker failure_worker(std::make_unique<FailureRecoveryBackend>(failure_state));
    require(failure_worker.start(true), "failure recovery worker did not start");

    RadioSendCommand failed_send;
    failed_send.id      = 77;
    failed_send.payload = {'n', 'o', 'a', 'c', 'k'};
    require(failure_worker.post(RadioCommand{std::move(failed_send)}) == RadioPostResult::Accepted,
            "failure recovery send was rejected");

    bool failed                 = false;
    const auto failure_deadline = Clock::now() + std::chrono::seconds(2);
    while (Clock::now() < failure_deadline && !failed) {
        RadioEvent failure_event;
        while (failure_worker.tryPopEvent(failure_event)) {
            if (const auto* tx = std::get_if<RadioTxFailedEvent>(&failure_event)) {
                failed = tx->id == 77;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(failed, "message without an acknowledgement did not fail");
    require(failure_state->data_attempts.load() == 5, "worker did not exhaust all acknowledgement attempts");
    require(failure_state->receive_starts.load() == 7,
            "worker did not explicitly reset RX after the final acknowledgement timeout");

    failure_state->link_available.store(true);
    RadioSendCommand recovered_send;
    recovered_send.id      = 78;
    recovered_send.payload = {'b', 'a', 'c', 'k'};
    require(failure_worker.post(RadioCommand{std::move(recovered_send)}) == RadioPostResult::Accepted,
            "recovered send was rejected");

    bool recovered                = false;
    const auto recovered_deadline = Clock::now() + std::chrono::seconds(2);
    while (Clock::now() < recovered_deadline && !recovered) {
        RadioEvent recovered_event;
        while (failure_worker.tryPopEvent(recovered_event)) {
            if (const auto* tx = std::get_if<RadioTxCompletedEvent>(&recovered_event)) {
                recovered = tx->id == 78;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    failure_worker.stop();

    require(recovered, "the same worker did not send successfully after the link recovered");
    require(failure_state->data_attempts.load() == 6, "recovered send did not succeed on its first attempt");
    return 0;
}
