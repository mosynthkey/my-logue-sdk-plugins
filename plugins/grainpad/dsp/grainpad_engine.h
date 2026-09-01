#pragma once

/*
 * File: grainpad_engine.h
 *
 * Live granular cloud engine for GrainPad. Multiple overlapping grains read
 * from embedded PCM with Hann envelopes. XY targets control scan position and
 * playback rate; panel knobs set grain size, density, spray, and release.
 */

#include "grainpad_pcm.h"
#include <math.h>
#include <stdint.h>

struct GrainPadGrain
{
  bool active = false;
  float read_pos = 0.f;
  float rate = 1.f;
  float gain = 1.f;
  uint32_t age = 0U;
  uint32_t length = 0U;

  static constexpr float kBaseRate = static_cast<float>(kGrainPadSampleRate) / 48000.f;

  static float pcmToFloat(int16_t sample)
  {
    return static_cast<float>(sample) * (1.f / 32768.f);
  }

  static float hermite(float y0, float y1, float y2, float y3, float frac)
  {
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + y1;
  }

  static uint32_t wrapIndex(int32_t index, uint32_t length)
  {
    int32_t wrapped = index % static_cast<int32_t>(length);
    if (wrapped < 0)
      wrapped += static_cast<int32_t>(length);
    return static_cast<uint32_t>(wrapped);
  }

  static float sampleAt(float position)
  {
    const int32_t index = static_cast<int32_t>(position);
    const float frac = position - static_cast<float>(index);
    const uint32_t i0 = wrapIndex(index - 1, kGrainPadLength);
    const uint32_t i1 = wrapIndex(index, kGrainPadLength);
    const uint32_t i2 = wrapIndex(index + 1, kGrainPadLength);
    const uint32_t i3 = wrapIndex(index + 2, kGrainPadLength);
    return hermite(
        pcmToFloat(kGrainPadPcm16[i0]),
        pcmToFloat(kGrainPadPcm16[i1]),
        pcmToFloat(kGrainPadPcm16[i2]),
        pcmToFloat(kGrainPadPcm16[i3]),
        frac);
  }

  static float hannEnvelope(uint32_t age, uint32_t length)
  {
    if (length <= 1U)
      return 0.f;
    const float phase = static_cast<float>(age) / static_cast<float>(length - 1U);
    return 0.5f * (1.f - cosf(phase * 6.2831853f));
  }

  void trigger(float center_pos, float pitch_ratio, float velocity_gain, uint32_t length_samples)
  {
    active = true;
    read_pos = center_pos;
    rate = pitch_ratio * kBaseRate;
    gain = velocity_gain;
    age = 0U;
    length = length_samples;
    if (length < 32U)
      length = 32U;
  }

  float render()
  {
    if (!active)
      return 0.f;

    const float envelope = hannEnvelope(age, length);
    float output = sampleAt(read_pos) * gain * envelope;

    read_pos += rate;
    const float loop_length = static_cast<float>(kGrainPadLength);
    while (read_pos >= loop_length)
      read_pos -= loop_length;
    while (read_pos < 0.f)
      read_pos += loop_length;

    ++age;
    if (age >= length)
    {
      active = false;
      return 0.f;
    }

    if (output > 1.f)
      output = 1.f;
    if (output < -1.f)
      output = -1.f;
    return output;
  }

  void reset() { active = false; }
};

class GrainPadEngine
{
public:
  static constexpr uint32_t kMaxGrains = 12U;
  static constexpr float kHostSampleRate = 48000.f;
  static constexpr float kOutputGain = 0.82f;

  enum
  {
    SCAN = 0U,
    PITCH,
    MIX,
    SIZE,
    DENS,
    SPRAY,
    DECAY,
    NUM_PARAMS
  };

  void init()
  {
    level_ = 1.f;
    mix_ = 1.f;
    scan_norm_ = 0.5f;
    pitch_ratio_ = 1.f;
    size_ms_ = 45.f;
    density_hz_ = 28.f;
    spray_norm_ = 0.35f;
    release_coeff_ = 0.99985f;
    scan_target_ = scan_norm_;
    pitch_target_ = pitch_ratio_;
    cloud_active_ = false;
    cloud_releasing_ = false;
    cloud_gain_ = 0.f;
    spawn_accum_ = 0.f;
    rng_state_ = 0x13579BDFU;
    clearGrains();
  }

  void reset()
  {
    cloud_active_ = false;
    cloud_releasing_ = false;
    cloud_gain_ = 0.f;
    clearGrains();
  }

  void setParameter(uint8_t index, int32_t value)
  {
    switch (index)
    {
    case SCAN:
      scan_norm_ = param10BitToNorm(value);
      if (!cloud_active_)
        scan_target_ = scan_norm_;
      break;
    case PITCH:
      pitch_ratio_ = pitchParamToRatio(value);
      if (!cloud_active_)
        pitch_target_ = pitch_ratio_;
      break;
    case MIX:
      mix_ = value / 1000.f;
      if (mix_ < 0.f)
        mix_ = 0.f;
      if (mix_ > 1.f)
        mix_ = 1.f;
      break;
    case SIZE:
      size_ms_ = sizeParamToMs(value);
      break;
    case DENS:
      density_hz_ = densityParamToHz(value);
      break;
    case SPRAY:
      spray_norm_ = param10BitToNorm(value);
      break;
    case DECAY:
      release_coeff_ = decayParamToCoeff(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const
  {
    (void)index;
    (void)value;
    return nullptr;
  }

  void setTouchTargets(float scan_norm, float pitch_ratio, bool active, bool releasing)
  {
    scan_target_ = scan_norm;
    pitch_target_ = pitch_ratio;
    cloud_active_ = active;
    cloud_releasing_ = releasing;
  }

  void renderBlock(float *out_mono, uint32_t frames)
  {
    const float scan_smooth = 0.08f;
    const float pitch_smooth = 0.06f;
    const float attack_inc = 1.f / (kHostSampleRate * 0.012f);

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      scan_norm_ += scan_smooth * (scan_target_ - scan_norm_);
      pitch_ratio_ += pitch_smooth * (pitch_target_ - pitch_ratio_);

      if (cloud_active_ && !cloud_releasing_)
      {
        cloud_gain_ += attack_inc;
        if (cloud_gain_ > 1.f)
          cloud_gain_ = 1.f;
      }
      else if (cloud_releasing_ || !cloud_active_)
      {
        cloud_gain_ *= release_coeff_;
        if (cloud_gain_ < 0.0005f)
        {
          cloud_gain_ = 0.f;
          if (cloud_releasing_)
          {
            cloud_releasing_ = false;
            clearGrains();
          }
        }
      }

      if (cloud_gain_ > 0.f && (cloud_active_ || cloud_releasing_))
        advanceSpawner(1.f / kHostSampleRate);

      float wet = 0.f;
      for (uint32_t grainIndex = 0; grainIndex < kMaxGrains; ++grainIndex)
        wet += grains_[grainIndex].render();
      wet *= cloud_gain_;

      out_mono[sampleIndex] = wet;
    }
  }

  float outputLevel() const { return level_ * kOutputGain; }

  float mix() const { return mix_; }

private:
  static float param10BitToNorm(int32_t value)
  {
    return static_cast<uint16_t>(value) * (1.f / 1023.f);
  }

  static float pitchParamToRatio(int32_t value)
  {
    const float norm = param10BitToNorm(value);
    const float octaves = (norm - 0.5f) * 4.f;
    return powf(2.f, octaves);
  }

  static float sizeParamToMs(int32_t value)
  {
    const float norm = param10BitToNorm(value);
    return 8.f + norm * norm * 192.f;
  }

  static float densityParamToHz(int32_t value)
  {
    const float norm = param10BitToNorm(value);
    return 4.f + norm * norm * 96.f;
  }

  static float decayParamToCoeff(int32_t value)
  {
    if (value <= 0)
      return 0.99995f;
    const float norm = param10BitToNorm(value);
    const float tau = 0.08f + norm * norm * 3.2f;
    return expf(-1.f / (tau * kHostSampleRate));
  }

  float nextUnitRandom()
  {
    rng_state_ = rng_state_ * 1664525U + 1013904223U;
    return static_cast<float>(rng_state_ >> 8) * (1.f / 16777216.f);
  }

  float randomSigned()
  {
    return nextUnitRandom() * 2.f - 1.f;
  }

  void clearGrains()
  {
    for (uint32_t grainIndex = 0; grainIndex < kMaxGrains; ++grainIndex)
      grains_[grainIndex].reset();
  }

  void advanceSpawner(float dt_sec)
  {
    if (density_hz_ <= 0.f)
      return;

    spawn_accum_ += density_hz_ * dt_sec;
    while (spawn_accum_ >= 1.f)
    {
      spawn_accum_ -= 1.f;
      spawnGrain();
    }
  }

  void spawnGrain()
  {
    uint32_t slot = kMaxGrains;
    for (uint32_t grainIndex = 0; grainIndex < kMaxGrains; ++grainIndex)
    {
      if (!grains_[grainIndex].active)
      {
        slot = grainIndex;
        break;
      }
    }
    if (slot >= kMaxGrains)
      slot = static_cast<uint32_t>(nextUnitRandom() * static_cast<float>(kMaxGrains));

    const float spread = spray_norm_ * 0.18f;
    const float center = scan_norm_ + randomSigned() * spread;
    float wrapped_center = center - floorf(center);
    if (wrapped_center < 0.f)
      wrapped_center += 1.f;
    const float read_pos = wrapped_center * static_cast<float>(kGrainPadLength);

    const float pitch_jitter = 1.f + randomSigned() * spray_norm_ * 0.08f;
    const float grain_pitch = pitch_ratio_ * pitch_jitter;
    const uint32_t length_samples = static_cast<uint32_t>(size_ms_ * 0.001f * kHostSampleRate);
    const float velocity = 0.65f + nextUnitRandom() * 0.35f;

    grains_[slot].trigger(read_pos, grain_pitch, velocity, length_samples);
  }

  uint32_t rng_state_ = 0x13579BDFU;
  float level_ = 1.f;
  float mix_ = 1.f;
  float scan_norm_ = 0.5f;
  float pitch_ratio_ = 1.f;
  float size_ms_ = 45.f;
  float density_hz_ = 28.f;
  float spray_norm_ = 0.35f;
  float release_coeff_ = 0.99985f;
  float scan_target_ = 0.5f;
  float pitch_target_ = 1.f;
  bool cloud_active_ = false;
  bool cloud_releasing_ = false;
  float cloud_gain_ = 0.f;
  float spawn_accum_ = 0.f;
  GrainPadGrain grains_[kMaxGrains];
};
