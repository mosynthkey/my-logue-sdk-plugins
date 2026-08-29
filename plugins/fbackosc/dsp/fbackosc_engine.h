#pragma once

/*
 * File: fbackosc_engine.h
 *
 * JP-8080-inspired Feedback oscillator engine.
 * Band-limited saw through a key-tracked resonant comb filter.
 * Platform-agnostic core used by NTS-1 mkII and microKORG2 targets.
 *
 */

#include "osc_api.h"
#include "utils/float_math.h"
#include <math.h>
#include <stdint.h>

class FBackOscEngine
{
public:
  static constexpr uint32_t kMaxDelaySamples = 8192U;
  static constexpr float kTwoPi = 6.283185307179586f;
  static constexpr float kOutputTrim = 0.55f;
  static constexpr float kMaxFeedback = 0.93f;

  struct Params
  {
    float harmonics = 0.5f;
    float feedback = 0.45f;
  };

  void reset()
  {
    saw_phase_ = 0.f;
    clearDelayLine();
    dc_state_ = 0.f;
  }

  void randomizePhase()
  {
    saw_phase_ = osc_white();
  }

  void setParams(const Params &params)
  {
    params_ = params;
    updateDerived();
  }

  const Params &getParams() const { return params_; }

  void setPitch(float w0, float note)
  {
    base_w0_ = w0;
    bl_idx_ = osc_bl_saw_idx(note);
    updateDerived();
  }

  float render()
  {
    const float saw_sample = osc_bl2_sawf(saw_phase_, bl_idx_);

    saw_phase_ += base_w0_;
    saw_phase_ -= floorf(saw_phase_);

    const float delayed = readDelay(delay_samples_);
    const float comb_sample = saw_sample + feedback_coeff_ * delayed;
    writeDelay(comb_sample);

    float output = comb_sample * kOutputTrim;
    output = softClip(output);

    // One-pole DC blocker
    const float blocked = output - dc_state_;
    dc_state_ = output + 0.995f * dc_state_;
    return blocked;
  }

private:
  static float softClip(float sample)
  {
    const float abs_sample = fabsf(sample);
    if (abs_sample < 1.f)
      return sample;
    return sample / (1.f + abs_sample - 1.f);
  }

  static float harmonicRatio(float harmonics_0_1)
  {
    const float clamped = (harmonics_0_1 < 0.f) ? 0.f : ((harmonics_0_1 > 1.f) ? 1.f : harmonics_0_1);
    return powf(2.f, (clamped - 0.5f) * 2.f);
  }

  void updateDerived()
  {
    feedback_coeff_ = params_.feedback * kMaxFeedback;

    const float ratio = harmonicRatio(params_.harmonics);
    if (base_w0_ > 1e-8f)
    {
      float delay = kTwoPi / (base_w0_ * ratio);
      if (delay < 2.f)
        delay = 2.f;
      if (delay > static_cast<float>(kMaxDelaySamples - 2U))
        delay = static_cast<float>(kMaxDelaySamples - 2U);
      delay_samples_ = delay;
    }
  }

  void clearDelayLine()
  {
    for (uint32_t sampleIndex = 0; sampleIndex < kMaxDelaySamples; ++sampleIndex)
      delay_line_[sampleIndex] = 0.f;
    write_pos_ = 0U;
  }

  float readDelay(float delaySamples) const
  {
    float read_pos = static_cast<float>(write_pos_) - delaySamples;
    while (read_pos < 0.f)
      read_pos += static_cast<float>(kMaxDelaySamples);

    const uint32_t index0 = static_cast<uint32_t>(read_pos) % kMaxDelaySamples;
    const uint32_t index1 = (index0 + 1U) % kMaxDelaySamples;
    const float frac = read_pos - floorf(read_pos);
    return linintf(frac, delay_line_[index0], delay_line_[index1]);
  }

  void writeDelay(float sample)
  {
    delay_line_[write_pos_] = sample;
    write_pos_ = (write_pos_ + 1U) % kMaxDelaySamples;
  }

  Params params_;
  float base_w0_ = 0.f;
  float bl_idx_ = 0.f;
  float feedback_coeff_ = 0.f;
  float delay_samples_ = 64.f;
  float saw_phase_ = 0.f;
  float dc_state_ = 0.f;
  float delay_line_[kMaxDelaySamples] = {};
  uint32_t write_pos_ = 0U;
};
