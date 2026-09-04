#pragma once

/*
 * File: technorumble.h
 *
 * Techno rumble kick processor: Schroeder reverb tail, sub LPF, soft-clip
 * drive, and input-transient sidechain duck. Stereo in/out insert FX.
 */

#include "macros.h"
#include "processor.h"
#include <math.h>
#include <stdint.h>

class TechnoRumble : public Processor
{
public:
  static constexpr uint32_t kCombSize = 4096U;
  static constexpr uint32_t kCombMask = kCombSize - 1U;
  static constexpr uint32_t kAllpassSize = 1024U;
  static constexpr uint32_t kAllpassMask = kAllpassSize - 1U;
  static constexpr uint32_t kCombLineCount = 4U;
  static constexpr uint32_t kAllpassLineCount = 2U;
  static constexpr float kOutputGain = 0.85f;
  static constexpr float kMaxPumpDepth = 0.9f;
  static constexpr float kTransientRatio = 2.2f;
  static constexpr float kTransientFloor = 0.004f;

  uint32_t getBufferSize() const override final
  {
    return kCombLineCount * kCombSize + kAllpassLineCount * kAllpassSize;
  }

  enum
  {
    DECAY = 0U,
    CUTOFF,
    MIX,
    DRIVE,
    PUMP,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case DECAY:
      decay_norm_ = param_10bit_to_f32(value);
      break;
    case CUTOFF:
      cutoff_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = value / 1000.f;
      if (mix_ < 0.f)
        mix_ = 0.f;
      if (mix_ > 1.f)
        mix_ = 1.f;
      break;
    case DRIVE:
      drive_norm_ = param_10bit_to_f32(value);
      break;
    case PUMP:
      pump_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
    updateDerivedParams();
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    (void)index;
    (void)value;
    return nullptr;
  }

  void init(float *allocated_buffer) override final
  {
    float *cursor = allocated_buffer;
    for (uint32_t combIndex = 0; combIndex < kCombLineCount; ++combIndex)
    {
      comb_lines_[combIndex] = cursor;
      cursor += kCombSize;
    }
    for (uint32_t allpassIndex = 0; allpassIndex < kAllpassLineCount; ++allpassIndex)
    {
      allpass_lines_[allpassIndex] = cursor;
      cursor += kAllpassSize;
    }

    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;

    write_index_ = 0U;
    lpf_state_ = 0.f;
    env_average_ = 0.f;
    duck_gain_ = 1.f;
    duck_hold_samples_ = 0U;
    updateDerivedParams();
  }

  void teardown() override final
  {
    for (uint32_t combIndex = 0; combIndex < kCombLineCount; ++combIndex)
      comb_lines_[combIndex] = nullptr;
    for (uint32_t allpassIndex = 0; allpassIndex < kAllpassLineCount; ++allpassIndex)
      allpass_lines_[allpassIndex] = nullptr;
  }

  void reset() override final
  {
    for (uint32_t combIndex = 0; combIndex < kCombLineCount; ++combIndex)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < kCombSize; ++sampleIndex)
        comb_lines_[combIndex][sampleIndex] = 0.f;
    }
    for (uint32_t allpassIndex = 0; allpassIndex < kAllpassLineCount; ++allpassIndex)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < kAllpassSize; ++sampleIndex)
        allpass_lines_[allpassIndex][sampleIndex] = 0.f;
    }

    write_index_ = 0U;
    lpf_state_ = 0.f;
    env_average_ = 0.f;
    duck_gain_ = 1.f;
    duck_hold_samples_ = 0U;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    const float dry_gain = 1.f - mix_;
    const float wet_gain = mix_ * kOutputGain;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      const float dry_left = in[0];
      const float dry_right = in[1];
      const float mono_in = (dry_left + dry_right) * 0.5f;

      updateSidechain(mono_in);

      float wet = processReverb(mono_in);
      wet = applyLowpass(wet);
      wet = applyDrive(wet);
      wet *= duck_gain_;

      const float wet_sample = wet * wet_gain;
      out[0] = dry_left * dry_gain + wet_sample;
      out[1] = dry_right * dry_gain + wet_sample;

      ++write_index_;
      in += 2;
      out += 2;
    }
  }

private:
  static constexpr uint32_t kCombDelays[kCombLineCount] = {2521U, 2765U, 3011U, 3259U};
  static constexpr uint32_t kAllpassDelays[kAllpassLineCount] = {727U, 947U};
  static constexpr float kAllpassGain = 0.5f;

  float *comb_lines_[kCombLineCount] = {};
  float *allpass_lines_[kAllpassLineCount] = {};
  uint32_t write_index_ = 0U;

  float decay_norm_ = 0.65f;
  float cutoff_norm_ = 0.35f;
  float mix_ = 1.f;
  float drive_norm_ = 0.45f;
  float pump_norm_ = 0.55f;

  float comb_feedback_ = 0.89f;
  float lpf_coeff_ = 0.02f;
  float drive_amount_ = 0.45f;
  float pump_amount_ = 0.55f;
  float duck_floor_gain_ = 1.f;

  float lpf_state_ = 0.f;
  float env_average_ = 0.f;
  float duck_gain_ = 1.f;
  uint32_t duck_hold_samples_ = 0U;

  void updateDerivedParams()
  {
    comb_feedback_ = 0.78f + decay_norm_ * 0.19f;
    const float cutoff_hz = 40.f + cutoff_norm_ * 460.f;
    lpf_coeff_ = 1.f - expf(-6.2831853f * cutoff_hz / getSampleRate());
    if (lpf_coeff_ < 0.f)
      lpf_coeff_ = 0.f;
    if (lpf_coeff_ > 1.f)
      lpf_coeff_ = 1.f;
    drive_amount_ = drive_norm_;
    pump_amount_ = pump_norm_;
    duck_floor_gain_ = 1.f - pump_amount_ * kMaxPumpDepth;
  }

  void updateSidechain(float mono_in)
  {
    const float abs_input = mono_in >= 0.f ? mono_in : -mono_in;
    env_average_ += (abs_input - env_average_) * 0.0025f;

    if (pump_amount_ > 0.f && abs_input > env_average_ * kTransientRatio + kTransientFloor)
    {
      duck_gain_ = duck_floor_gain_;
      duck_hold_samples_ = static_cast<uint32_t>(getSampleRate() * 0.006f);
    }

    if (duck_hold_samples_ > 0U)
    {
      --duck_hold_samples_;
      return;
    }

    if (duck_gain_ < 1.f)
    {
      duck_gain_ += (1.f - duck_gain_) * 0.00035f;
      if (duck_gain_ > 0.999f)
        duck_gain_ = 1.f;
    }
  }

  float processComb(uint32_t combIndex, float input)
  {
    float *line = comb_lines_[combIndex];
    const uint32_t delay = kCombDelays[combIndex];
    const uint32_t read_index = (write_index_ + kCombSize - delay) & kCombMask;
    const float delayed = line[read_index];
    const float output = input + comb_feedback_ * delayed;
    line[write_index_ & kCombMask] = output;
    return output;
  }

  float processAllpass(uint32_t allpassIndex, float input)
  {
    float *line = allpass_lines_[allpassIndex];
    const uint32_t delay = kAllpassDelays[allpassIndex];
    const uint32_t read_index = (write_index_ + kAllpassSize - delay) & kAllpassMask;
    const float delayed = line[read_index];
    const float output = -kAllpassGain * input + delayed;
    line[write_index_ & kAllpassMask] = input + kAllpassGain * delayed;
    return output;
  }

  float processReverb(float input)
  {
    float sum = 0.f;
    for (uint32_t combIndex = 0; combIndex < kCombLineCount; ++combIndex)
      sum += processComb(combIndex, input);
    sum *= 0.25f;

    for (uint32_t allpassIndex = 0; allpassIndex < kAllpassLineCount; ++allpassIndex)
      sum = processAllpass(allpassIndex, sum);

    return sum;
  }

  float applyLowpass(float input)
  {
    lpf_state_ += lpf_coeff_ * (input - lpf_state_);
    return lpf_state_;
  }

  float applyDrive(float input) const
  {
    const float driven = input * (1.f + drive_amount_ * 6.f);
    const float abs_driven = driven >= 0.f ? driven : -driven;
    return driven / (1.f + abs_driven * (0.5f + drive_amount_));
  }
};
