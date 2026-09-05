#pragma once

/*
 * Small shared helpers for NTS-3 genericfx DSP.
 * Keep this header-only and libm-free (logue float_math only).
 */

#include "utils/float_math.h"
#include <stdint.h>

namespace fx
{

inline float clip01(float value)
{
  if (value < 0.f)
    return 0.f;
  if (value > 1.f)
    return 1.f;
  return value;
}

inline float clip(float value, float min_value, float max_value)
{
  if (value < min_value)
    return min_value;
  if (value > max_value)
    return max_value;
  return value;
}

inline float mix(float dry, float wet, float amount)
{
  return dry + (wet - dry) * amount;
}

inline float absf(float value)
{
  return si_fabsf(value);
}

inline float softclip(float value)
{
  return fastertanhf(value);
}

inline float onePoleCoeff(float hz, float sample_rate)
{
  const float clamped = clip(hz, 1.f, sample_rate * 0.45f);
  return 1.f - fasterexpf(-6.283185307179586f * clamped / sample_rate);
}

inline float noteToHz(float midi_note)
{
  return 440.f * fasterpow2f((midi_note - 69.f) * (1.f / 12.f));
}

inline float noteToInc(float midi_note, float sample_rate)
{
  return noteToHz(midi_note) / sample_rate;
}

inline uint32_t nextRandom(uint32_t &state)
{
  state = state * 1664525U + 1013904223U;
  return state;
}

inline float randomFloat(uint32_t &state)
{
  return static_cast<float>(nextRandom(state) >> 8) * (1.f / 16777216.f);
}

inline bool euclidHit(uint32_t step_index, uint32_t hits, uint32_t steps)
{
  if (steps == 0U || hits == 0U)
    return false;
  if (hits >= steps)
    return true;
  return ((step_index * hits) % steps) < hits;
}

inline float wrap01(float phase)
{
  if (phase >= 1.f)
    phase -= static_cast<float>(static_cast<int32_t>(phase));
  if (phase < 0.f)
    phase += 1.f;
  return phase;
}

inline float polyBlep(float phase, float increment)
{
  if (phase < increment)
  {
    const float t = phase / increment;
    return t + t - t * t - 1.f;
  }
  if (phase > 1.f - increment)
  {
    const float t = (phase - 1.f) / increment;
    return t * t + t + t + 1.f;
  }
  return 0.f;
}

inline float blepSaw(float phase, float increment)
{
  return (2.f * phase - 1.f) - polyBlep(phase, increment);
}

inline float blepPulse(float phase, float increment, float width)
{
  float pulse = (phase < width) ? 1.f : -1.f;
  pulse += polyBlep(phase, increment);
  float trailing = phase - width;
  if (trailing < 0.f)
    trailing += 1.f;
  pulse -= polyBlep(trailing, increment);
  return pulse;
}

inline void pickLive(const float *in, const float *raw, float &left, float &right)
{
  left = in[0];
  right = in[1];
  if (raw == nullptr)
    return;
  const float in_energy = absf(in[0]) + absf(in[1]);
  const float raw_energy = absf(raw[0]) + absf(raw[1]);
  if (raw_energy > in_energy)
  {
    left = raw[0];
    right = raw[1];
  }
}

inline uint32_t samplesPerBeat(float bpm, float sample_rate)
{
  const float clamped = clip(bpm, 40.f, 300.f);
  return static_cast<uint32_t>(sample_rate * 60.f / clamped);
}

struct OnePole
{
  float z = 0.f;

  float processLp(float input, float coeff)
  {
    z += coeff * (input - z);
    return z;
  }

  float processHp(float input, float coeff)
  {
    return input - processLp(input, coeff);
  }
};

struct EnvelopeFollower
{
  float env = 0.f;

  float process(float input, float attack, float release)
  {
    const float rectified = absf(input);
    const float coeff = (rectified > env) ? attack : release;
    env += coeff * (rectified - env);
    return env;
  }
};

struct AdsrLite
{
  float level = 0.f;
  bool gated = false;

  void gate(bool on)
  {
    gated = on;
    if (on && level < 0.001f)
      level = 0.001f;
  }

  float process(float attack, float release)
  {
    const float target = gated ? 1.f : 0.f;
    const float coeff = gated ? attack : release;
    level += coeff * (target - level);
    return level;
  }
};

} // namespace fx
