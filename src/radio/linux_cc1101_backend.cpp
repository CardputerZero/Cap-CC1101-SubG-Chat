#include "radio/radio_backend.hpp"

#include <stdexcept>

#if defined(CC1101_CHAT_ENABLE_LINUX_RADIO) && CC1101_CHAT_ENABLE_LINUX_RADIO && defined(__linux__)

#include "hal/cardputerzero_cc1101_power.hpp"
#include "radio/driver/cc1101.h"
#include "radio/driver/spi_device.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace cc1101_chat::radio {
namespace {

using Clock = std::chrono::steady_clock;

uint64_t monotonicMilliseconds()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
}

void cancellableSleep(std::chrono::milliseconds duration, const CancellationToken& cancellation)
{
    const auto deadline = Clock::now() + duration;
    while (Clock::now() < deadline) {
        cancellation.throwIfCancellationRequested();
        std::this_thread::sleep_for(
            std::min(std::chrono::milliseconds(10),
                     std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now())));
    }
    cancellation.throwIfCancellationRequested();
}

class LinuxCc1101Backend final : public RadioBackend {
public:
    RadioInfo open(const CancellationToken& cancellation) override
    {
        close();
        cancellation.throwIfCancellationRequested();

        std::string_view stage = "startup";
        try {
            stage = "Cap power enable";
            spdlog::info("CC1101 backend: enabling Cap power (pinctrl G14/G15/G26, GPIO26 and ext_5v_out LED class)");
            std::string power_error;
            if (!_power.enable(power_error, cancellation.nativeFlag())) {
                cancellation.throwIfCancellationRequested();
                throw std::runtime_error("CC1101 power enable failed: " + power_error);
            }

            stage = "power settle";
            spdlog::debug("CC1101 backend: waiting 100 ms for Cap power to settle");
            cancellableSleep(std::chrono::milliseconds(100), cancellation);

            stage = "SPI open";
            spdlog::info("CC1101 backend: opening SPI /dev/spidev0.1 (mode=0, speed=500000 Hz, bits=8, kernel CS=yes)");
            _spi = std::make_unique<SpiDevice>();
            _spi->openDevice("/dev/spidev0.1", 500000, 0, 8, false);

            stage = "GPIO setup and CC1101 probe/configuration";
            BoardPins pins;
            pins.cs_gpio     = -1;
            pins.gdo0_gpio   = 15;
            pins.rf_sw0_gpio = 14;
            pins.reset_gpio  = -1;
            spdlog::info(
                "CC1101 backend: GPIO topology (chip=/dev/gpiochip0, CS=kernel, GDO0=line 15, RF_SW0=line 14, "
                "reset=software SRES)");

            _radio = std::make_unique<CC1101Radio>(*_spi, pins);
            RadioConfig config;
            config.freq_mhz       = 868.0F;
            config.bit_rate_kbps  = 2.4F;
            config.freq_dev_khz   = 25.4F;
            config.rx_bw_khz      = 58.0F;
            config.power_dbm      = 10;
            config.preamble_bytes = 16;
            spdlog::info(
                "CC1101 backend: applying radio profile (2-FSK, frequency={:.3f} MHz, bitrate={:.3f} kbps, "
                "bandwidth={:.1f} kHz, deviation={:.1f} kHz, power={} dBm, sync=0x12AD)",
                config.freq_mhz, config.bit_rate_kbps, config.rx_bw_khz, config.freq_dev_khz,
                static_cast<int>(config.power_dbm));
            _radio->begin(config, cancellation.nativeFlag());

            stage                  = "chip information readback";
            _info.backend_name     = "CC1101 /dev/spidev0.1";
            _info.mock             = false;
            _info.chip_version     = _radio->chipVersion();
            _info.frequency_mhz    = config.freq_mhz;
            _info.bit_rate_kbps    = config.bit_rate_kbps;
            _info.rx_bandwidth_khz = config.rx_bw_khz;
            _info.deviation_khz    = config.freq_dev_khz;
            _info.output_power_dbm = config.power_dbm;
            _open                  = true;
            cancellation.throwIfCancellationRequested();
            spdlog::info("CC1101 backend: open complete (VERSION=0x{:02X})", static_cast<unsigned>(_info.chip_version));
            return _info;
        } catch (const RadioCancelled&) {
            close();
            throw;
        } catch (const CC1101Cancelled&) {
            close();
            throw RadioCancelled{};
        } catch (const std::exception& exception) {
            const std::string message =
                "CC1101 initialization failed at " + std::string(stage) + ": " + exception.what();
            spdlog::error("{}", message);
            close();
            throw std::runtime_error(message);
        } catch (...) {
            spdlog::error("CC1101 initialization failed at {}: unknown error", stage);
            close();
            throw;
        }
    }

    void close() noexcept override
    {
        _receiving = false;
        if (_radio) {
            try {
                _radio->idle();
            } catch (...) {
            }
            _radio.reset();
        }
        if (_spi) {
            _spi->closeDevice();
            _spi.reset();
        }
        _power.disable();
        _open = false;
    }

    void startReceive(const CancellationToken& cancellation) override
    {
        cancellation.throwIfCancellationRequested();
        requireOpen();
        try {
            spdlog::info("CC1101 backend: requesting RX state");
            _radio->startReceive();
            cancellation.throwIfCancellationRequested();
            const uint8_t state = _radio->marcState();
            if (state == 0x0D) {
                spdlog::info("CC1101 backend: RX active (MARCSTATE=0x{:02X})", static_cast<unsigned>(state));
            } else {
                spdlog::warn("CC1101 backend: RX requested, unexpected MARCSTATE=0x{:02X} (expected 0x0D)",
                             static_cast<unsigned>(state));
            }
            _receiving = true;
        } catch (const RadioCancelled&) {
            throw;
        } catch (const std::exception& exception) {
            _receiving              = false;
            const std::string error = "CC1101 start RX failed: " + std::string(exception.what());
            spdlog::error("{}", error);
            throw std::runtime_error(error);
        }
    }

    void stopReceive() override
    {
        requireOpen();
        _radio->idle();
        _receiving = false;
    }

    bool receive(RadioPacket& packet, std::chrono::milliseconds timeout, const CancellationToken& cancellation) override
    {
        cancellation.throwIfCancellationRequested();
        requireOpen();
        if (!_receiving) {
            return false;
        }

        const auto count = std::clamp<int64_t>(timeout.count(), 0, std::numeric_limits<int>::max());
        RxPacket received;
        try {
            if (!_radio->receive(received, static_cast<int>(count), cancellation.nativeFlag())) {
                return false;
            }
        } catch (const CC1101Cancelled&) {
            throw RadioCancelled{};
        }

        packet.sequence     = ++_sequence;
        packet.timestamp_ms = monotonicMilliseconds();
        packet.data         = std::move(received.data);
        packet.rssi_dbm     = received.rssi_dbm;
        packet.lqi          = received.lqi;
        packet.crc_ok       = received.crc_ok;
        return true;
    }

    void transmit(const std::vector<uint8_t>& payload, const CancellationToken& cancellation) override
    {
        cancellation.throwIfCancellationRequested();
        requireOpen();
        if (payload.empty()) {
            throw std::runtime_error("payload is empty");
        }
        if (payload.size() > kMaxPayloadSize) {
            throw std::runtime_error("payload exceeds the 61-byte receive-safe limit");
        }

        try {
            _radio->transmit(payload.data(), payload.size(), 1000, cancellation.nativeFlag());
        } catch (const CC1101Cancelled&) {
            throw RadioCancelled{};
        }
        cancellation.throwIfCancellationRequested();
    }

private:
    CardputerZeroCc1101Power _power;
    std::unique_ptr<SpiDevice> _spi;
    std::unique_ptr<CC1101Radio> _radio;
    RadioInfo _info;
    bool _open         = false;
    bool _receiving    = false;
    uint64_t _sequence = 0;

    void requireOpen() const
    {
        if (!_open || !_radio || !_spi) {
            throw std::runtime_error("CC1101 backend is not open");
        }
    }
};

}  // namespace

std::unique_ptr<RadioBackend> makeLinuxCc1101Backend()
{
    return std::make_unique<LinuxCc1101Backend>();
}

}  // namespace cc1101_chat::radio

#else

namespace cc1101_chat::radio {

std::unique_ptr<RadioBackend> makeLinuxCc1101Backend()
{
    throw std::runtime_error(
        "Linux CC1101 backend is unavailable; build with CC1101_CHAT_ENABLE_LINUX_RADIO=1 on Linux");
}

}  // namespace cc1101_chat::radio

#endif
