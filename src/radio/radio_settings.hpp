#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace cc1101_chat::radio {

// One channel per band supported by the Cap's RF matching network.
inline constexpr std::array kFrequenciesKhz{315000, 433920, 868000, 915000};
inline constexpr std::array kPowersDbm{-30, -20, -15, -10, 0, 5, 7, 10};
inline constexpr int kUnsetPowerDbm = -31;

template <std::size_t N>
constexpr std::size_t presetIndex(const std::array<int, N>& values, int value)
{
    for (std::size_t i = 0; i < N; ++i) {
        if (values[i] == value) return i;
    }
    return N;
}

template <std::size_t N>
constexpr int stepPreset(const std::array<int, N>& values, int value, bool forward)
{
    const auto index = presetIndex(values, value);
    if (index == N) return forward ? values.front() : values.back();
    return values[(index + (forward ? 1 : N - 1)) % N];
}

struct RadioSettings {
    int frequencyKhz = 0;
    int powerDbm     = kUnsetPowerDbm;

    constexpr bool hasFrequency() const
    {
        return presetIndex(kFrequenciesKhz, frequencyKhz) < kFrequenciesKhz.size();
    }
    constexpr bool hasPower() const
    {
        return presetIndex(kPowersDbm, powerDbm) < kPowersDbm.size();
    }
    constexpr bool complete() const
    {
        return hasFrequency() && hasPower();
    }
    constexpr float frequencyMhz() const
    {
        return frequencyKhz / 1000.0F;
    }
    constexpr bool operator==(const RadioSettings& other) const
    {
        return frequencyKhz == other.frequencyKhz && powerDbm == other.powerDbm;
    }
};

}  // namespace cc1101_chat::radio
