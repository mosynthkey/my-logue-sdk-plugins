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
  static constexpr float kOutputTrim = 0.55f;
  static constexpr float kMaxFeedback = 0.93f;
  static constexpr float kParameterSmoothing = 0.002f;

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
    feedback_coeff_ = target_feedback_coeff_;
    delay_samples_ = target_delay_samples_;
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
    bl_idx_ = bandLimitedSawIndex(note);
    updateDerived();
  }

  float render()
  {
    feedback_coeff_ += (target_feedback_coeff_ - feedback_coeff_) * kParameterSmoothing;
    delay_samples_ += (target_delay_samples_ - delay_samples_) * kParameterSmoothing;

    const float saw_sample = osc_bl2_sawf(saw_phase_, bl_idx_);

    saw_phase_ += base_w0_;
    saw_phase_ -= floorf(saw_phase_);

    const float delayed = readDelay(delay_samples_);
    float comb_sample = saw_sample + feedback_coeff_ * delayed;
    if (comb_sample > 4.f)
      comb_sample = 4.f;
    else if (comb_sample < -4.f)
      comb_sample = -4.f;
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
    const float exponent = (clamped - 0.5f) * 2.f;
    const float x = exponent * 0.6931471805599453f;
    return 1.f + x * (1.f + x * (0.5f + x * (0.1666666667f + x * (0.0416666667f + x * 0.0083333333f))));
  }

  static float bandLimitedSawIndex(float note)
  {
    uint32_t index = 0U;
    while (index < k_wt_saw_notes_cnt - 1U && static_cast<float>(wt_saw_notes[index]) < note)
      ++index;

    const uint8_t previous = index > 0U ? wt_saw_notes[index - 1U] : 0U;
    const float interval = static_cast<float>(wt_saw_notes[index] - previous);
    if (interval <= 0.f)
      return static_cast<float>(index);
    const float fractional = static_cast<float>(index) + (note - static_cast<float>(previous)) / interval;
    const float maximum = static_cast<float>(k_wt_saw_notes_cnt - 1U);
    return fractional < maximum ? fractional : maximum;
  }

  void updateDerived()
  {
    target_feedback_coeff_ = params_.feedback * kMaxFeedback;

    const float ratio = harmonicRatio(params_.harmonics);
    if (base_w0_ > 1e-8f)
    {
      // w0 is cycles/sample (osc_w0f_for_note). Comb period is 1 / (f * ratio).
      float delay = 1.f / (base_w0_ * ratio);
      if (delay < 2.f)
        delay = 2.f;
      if (delay > static_cast<float>(kMaxDelaySamples - 2U))
        delay = static_cast<float>(kMaxDelaySamples - 2U);
      target_delay_samples_ = delay;
    }
    else
    {
      target_delay_samples_ = static_cast<float>(kMaxDelaySamples - 2U);
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
  float target_feedback_coeff_ = 0.f;
  float delay_samples_ = 64.f;
  float target_delay_samples_ = 64.f;
  float saw_phase_ = 0.f;
  float dc_state_ = 0.f;
  float delay_line_[kMaxDelaySamples] = {};
  uint32_t write_pos_ = 0U;
};
