#pragma once

/*
 * File: fbackosc_engine.h
 *
 * JP-8080-inspired Feedback oscillator engine.
 * Band-limited saw through a key-tracked resonant comb filter.
 * Platform-agnostic core used by NTS-1 mkII, microKORG2, and MoHowl.
 *
 */

#ifndef FBACKOSC_MAX_DELAY
#define FBACKOSC_MAX_DELAY 8192U
#endif

#ifndef FBACKOSC_NO_OSC_API
#include "osc_api.h"
#endif

#include <stdint.h>

class FBackOscEngine
{
public:
  static constexpr uint32_t kMaxDelaySamples = FBACKOSC_MAX_DELAY;
  // Single saw sits near -7.5 dBFS. Comb peak gain 1/(1-fb) is compensated
  // in render() so FEED changes timbre, not a 20 dB jump.
  static constexpr float kOutputTrim = 0.42f;
  static constexpr float kMaxFeedback = 0.93f;
  static constexpr float kParameterSmoothing = 0.002f;
  static constexpr float kLoopDamping = 0.97f;
  static constexpr float kLoopDcPole = 0.002f;
  static constexpr float kOutputDcCoeff = 0.995f;
  static constexpr float kCompensateKeep = 0.42f;

  struct Params
  {
    float harmonics = 0.5f;
    float feedback = 0.45f;
  };

  void reset()
  {
    saw_phase_ = 0.f;
    clearDelayLine();
    dc_x_ = 0.f;
    dc_y_ = 0.f;
    loop_dc_ = 0.f;
    feedback_coeff_ = target_feedback_coeff_;
    delay_samples_ = target_delay_samples_;
  }

  void randomizePhase()
  {
#ifdef FBACKOSC_NO_OSC_API
    phase_rng_ = phase_rng_ * 1664525U + 1013904223U;
    saw_phase_ = static_cast<float>(phase_rng_ >> 8) * (1.f / 16777216.f);
#else
    saw_phase_ = osc_white();
    if (saw_phase_ < 0.f)
      saw_phase_ = saw_phase_ * 0.5f + 0.5f;
#endif
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
#ifndef FBACKOSC_NO_OSC_API
    bl_idx_ = bandLimitedSawIndex(note);
#else
    (void)note;
#endif
    updateDerived();
  }

  float render()
  {
    feedback_coeff_ += (target_feedback_coeff_ - feedback_coeff_) * kParameterSmoothing;
    delay_samples_ += (target_delay_samples_ - delay_samples_) * kParameterSmoothing;

    const float saw_sample = generateSaw();

    const float delayed = readDelay(delay_samples_);
    loop_dc_ += kLoopDcPole * (delayed - loop_dc_);
    const float delayed_ac = delayed - loop_dc_;

    float comb_sample = saw_sample + feedback_coeff_ * delayed_ac * kLoopDamping;
    if (comb_sample > 8.f)
      comb_sample = 8.f;
    else if (comb_sample < -8.f)
      comb_sample = -8.f;
    writeDelay(comb_sample);

    const float one_minus_fb = 1.f - feedback_coeff_;
    const float compensate = one_minus_fb / (1.f - feedback_coeff_ * kCompensateKeep);
    float output = comb_sample * kOutputTrim * compensate;
    output = softClip(output);

    const float blocked = output - dc_x_ + kOutputDcCoeff * dc_y_;
    dc_x_ = output;
    dc_y_ = blocked;
    return blocked;
  }

private:
  static float lerp(float frac, float a, float b)
  {
    return a + frac * (b - a);
  }

  static float softClip(float sample)
  {
    float x = sample;
    if (x > 1.f)
      x = 1.f;
    else if (x < -1.f)
      x = -1.f;
    return x - x * x * x * (1.f / 3.f);
  }

#ifdef FBACKOSC_NO_OSC_API
  static float polyBlep(float phase, float increment)
  {
    if (increment < 1e-8f)
      return 0.f;
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
#endif

  float generateSaw()
  {
#ifdef FBACKOSC_NO_OSC_API
    const float saw_sample = (2.f * saw_phase_ - 1.f) - polyBlep(saw_phase_, base_w0_);
#else
    const float saw_sample = osc_bl2_sawf(saw_phase_, bl_idx_);
#endif
    saw_phase_ += base_w0_;
    if (saw_phase_ >= 1.f)
      saw_phase_ -= 1.f;
    return saw_sample;
  }

  static float harmonicRatio(float harmonics_0_1)
  {
    const float clamped = (harmonics_0_1 < 0.f) ? 0.f : ((harmonics_0_1 > 1.f) ? 1.f : harmonics_0_1);
    const float exponent = (clamped - 0.5f) * 2.f;
    const float x = exponent * 0.6931471805599453f;
    return 1.f + x * (1.f + x * (0.5f + x * (0.1666666667f + x * (0.0416666667f + x * 0.0083333333f))));
  }

#ifndef FBACKOSC_NO_OSC_API
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
#endif

  void updateDerived()
  {
    target_feedback_coeff_ = params_.feedback * kMaxFeedback;

    const float ratio = harmonicRatio(params_.harmonics);
    if (base_w0_ > 1e-8f)
    {
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
    const float frac = read_pos - static_cast<float>(index0);
    return lerp(frac, delay_line_[index0], delay_line_[index1]);
  }

  void writeDelay(float sample)
  {
    delay_line_[write_pos_] = sample;
    write_pos_ = (write_pos_ + 1U) % kMaxDelaySamples;
  }

  Params params_;
  float base_w0_ = 0.f;
#ifndef FBACKOSC_NO_OSC_API
  float bl_idx_ = 0.f;
#endif
  float feedback_coeff_ = 0.f;
  float target_feedback_coeff_ = 0.f;
  float delay_samples_ = 64.f;
  float target_delay_samples_ = 64.f;
  float saw_phase_ = 0.f;
  float dc_x_ = 0.f;
  float dc_y_ = 0.f;
  float loop_dc_ = 0.f;
  float delay_line_[kMaxDelaySamples] = {};
  uint32_t write_pos_ = 0U;
#ifdef FBACKOSC_NO_OSC_API
  uint32_t phase_rng_ = 0xA341316CU;
#endif
};
