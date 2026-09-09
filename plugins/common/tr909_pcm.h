#pragma once

/*
 * Shared TR-909 PCM board helpers (Hi-Hat / Ride style paths).
 * Header-only and libm-free. ROM tables live in separate headers.
 */

#include <stdint.h>

namespace tr909
{

constexpr float kRomClockHz = 30000.f;
constexpr float kHostRateHz = 48000.f;
constexpr float kRomPhaseInc = kRomClockHz / kHostRateHz;

// 6-bit resistor DAC (unsigned offset-binary, midpoint 32).
constexpr float kDacMid = 32.f;
constexpr float kDacScale = 1.f / 32.f;

// 9090 transistor reconstruction poles at 48 kHz: 1-exp(-2π fc/fs).
constexpr float kLpfACoeff = 0.5378f; // ~5.9 kHz
constexpr float kLpfBCoeff = 0.9549f; // ~23.7 kHz
constexpr float kDcCoeff = 0.99608f;  // ~30 Hz DC block

// Lightweight single-pole used by kit units that share the HH crop ROM.
constexpr float kSimpleLpfCoeff = 0.55f;

inline uint8_t readPacked6(const uint8_t *packed, uint32_t sample_index)
{
  const uint32_t bit_index = sample_index * 6U;
  const uint32_t byte_index = bit_index >> 3;
  const uint32_t shift = bit_index & 7U;
  const uint32_t pair = static_cast<uint32_t>(packed[byte_index]) |
                        (static_cast<uint32_t>(packed[byte_index + 1U]) << 8);
  return static_cast<uint8_t>((pair >> shift) & 0x3FU);
}

inline float dacFromCode(uint8_t code)
{
  return (static_cast<float>(code) - kDacMid) * kDacScale;
}

inline float dacFromPacked(const uint8_t *packed, uint32_t sample_index)
{
  return dacFromCode(readPacked6(packed, sample_index));
}

// 4th-order e^u with u = x*ln2 — Tune / pitch clock (no libm).
inline float exp2Approx(float x)
{
  const float u = x * 0.69314718f;
  return 1.f + u * (1.f + u * (0.5f + u * (0.16666667f + u * 0.041666668f)));
}

// Per-sample multiply near 1. Prefer over fasterexpf for tiny |x|.
inline float envCoeffNearOne(float seconds, float sample_rate)
{
  if (seconds <= 1.0e-6f || sample_rate <= 0.f)
    return 0.f;
  return 1.f - 1.f / (seconds * sample_rate);
}

inline float dcBlock(float input, float &prev_in, float &prev_out)
{
  const float output = input - prev_in + kDcCoeff * prev_out;
  prev_in = input;
  prev_out = output;
  return output;
}

} // namespace tr909
