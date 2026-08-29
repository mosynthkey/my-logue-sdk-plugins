#pragma once

/*
 * File: hypersaw_engine.h
 *
 * Virus TI HyperSaw-inspired multi-saw oscillator engine.
 * Platform-agnostic core used by NTS-1 mkII and microKORG2 targets.
 *
 */

#include "osc_api.h"
#include <math.h>
#include <stdint.h>

#ifndef HYPERSAW_DIAGNOSTIC
#define HYPERSAW_DIAGNOSTIC 0
#endif

class HyperSawEngine
{
public:
  static constexpr uint32_t kVoiceCount = 9U;
  static constexpr float kTwoPi = 6.283185307179586f;
  static constexpr float kOutputTrim = 0.34f;

  struct Params
  {
    float density = 5.f;
    float spread = 0.45f;
    float sub_mix = 0.f;
    float width = 0.75f;
  };

  void reset()
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
    {
      saw_phase_[voiceIndex] = 0.f;
      sub_phase_[voiceIndex] = 0.f;
    }
  }

  void randomizePhases()
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
    {
#if HYPERSAW_DIAGNOSTIC >= 3
      saw_phase_[voiceIndex] = static_cast<float>(voiceIndex + 1U) / static_cast<float>(kVoiceCount + 1U);
      sub_phase_[voiceIndex] = static_cast<float>(kVoiceCount - voiceIndex) / static_cast<float>(kVoiceCount + 1U);
#else
      saw_phase_[voiceIndex] = osc_white();
      sub_phase_[voiceIndex] = osc_white();
#endif
    }
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

  float renderMono()
  {
    float left = 0.f;
    float right = 0.f;
    renderStereo(left, right);
    return 0.5f * (left + right);
  }

  void renderStereo(float &left, float &right)
  {
    float main_left = 0.f;
    float main_right = 0.f;
    float sub_left = 0.f;
    float sub_right = 0.f;

    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
    {
      const float voice_gain = voice_gain_[voiceIndex];
      if (voice_gain <= 0.f)
        continue;

#if HYPERSAW_DIAGNOSTIC >= 2
      const float saw_sample = (2.f * saw_phase_[voiceIndex] - 1.f) * voice_gain;
      const float sub_sample = (sub_phase_[voiceIndex] < 0.5f ? 1.f : -1.f) * voice_gain;
#else
      const float saw_sample = osc_bl2_sawf(saw_phase_[voiceIndex], bl_idx_) * voice_gain;
      const float sub_sample = osc_bl2_sqrf(sub_phase_[voiceIndex], bl_idx_) * voice_gain;
#endif

      main_left += saw_sample * pan_left_[voiceIndex];
      main_right += saw_sample * pan_right_[voiceIndex];
      sub_left += sub_sample * pan_left_[voiceIndex];
      sub_right += sub_sample * pan_right_[voiceIndex];

      saw_phase_[voiceIndex] += w0_[voiceIndex];
      saw_phase_[voiceIndex] -= static_cast<float>(static_cast<uint32_t>(saw_phase_[voiceIndex]));
      sub_phase_[voiceIndex] += sub_w0_[voiceIndex];
      sub_phase_[voiceIndex] -= static_cast<float>(static_cast<uint32_t>(sub_phase_[voiceIndex]));
    }

    const float sub_amount = params_.sub_mix;
    left = (main_left * (1.f - sub_amount) + sub_left * sub_amount) * norm_gain_;
    right = (main_right * (1.f - sub_amount) + sub_right * sub_amount) * norm_gain_;
  }

  static float spreadCurve(float spread_0_1)
  {
    const float clamped = (spread_0_1 < 0.f) ? 0.f : ((spread_0_1 > 1.f) ? 1.f : spread_0_1);
    static const float kSpreadLut[17] = {
        0.f, 0.00967268f, 0.0220363f, 0.0339636f, 0.0467636f, 0.0591273f, 0.0714909f,
        0.0838545f, 0.0967273f, 0.121527f, 0.147127f, 0.193455f, 0.243418f, 0.293382f,
        0.343345f, 0.3928f, 1.f};
    const float scaled = clamped * 16.f;
    const uint32_t lutIndex = static_cast<uint32_t>(scaled);
    const float frac = scaled - static_cast<float>(lutIndex);
    if (lutIndex >= 16U)
      return 1.f;
    return linintf(frac, kSpreadLut[lutIndex], kSpreadLut[lutIndex + 1U]);
  }

  static float densityGain(float density, uint32_t voiceIndex)
  {
    const float voice_level = density - static_cast<float>(voiceIndex);
    if (voice_level <= 0.f)
      return 0.f;
    if (voice_level >= 1.f)
      return 1.f;
    return voice_level;
  }

  static float bandLimitedSawIndex(float note)
  {
    uint32_t index = 0U;
    while (index < k_wt_saw_notes_cnt - 1U && static_cast<float>(wt_saw_notes[index]) < note)
      ++index;

    const uint8_t previous = index > 0U ? wt_saw_notes[index - 1U] : 0U;
    const float interval = static_cast<float>(wt_saw_notes[index] - previous);
    const float fractional = static_cast<float>(index) + (note - static_cast<float>(previous)) / interval;
    const float maximum = static_cast<float>(k_wt_saw_notes_cnt - 1U);
    return fractional < maximum ? fractional : maximum;
  }

  void updateDerived()
  {
    const float spread_amount = spreadCurve(params_.spread);
    const float width = (params_.width < 0.f) ? 0.f : ((params_.width > 1.f) ? 1.f : params_.width);

    static const float kDetuneCoeff[kVoiceCount] = {
        0.f,
        -128.f, 128.f,
        -408.f, 408.f,
        -704.f, 704.f,
        -960.f, 960.f};

    static const float kPanSpread[kVoiceCount] = {
        0.f,
        -0.72f, 0.72f,
        -0.52f, 0.52f,
        -0.34f, 0.34f,
        -0.88f, 0.88f};

    float energy = 0.f;
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
    {
      voice_gain_[voiceIndex] = densityGain(params_.density, voiceIndex);

      const float detune_ratio = 1.f + (kDetuneCoeff[voiceIndex] * spread_amount) * (1.f / 720.f);
      const float voice_w0 = base_w0_ * detune_ratio;
      w0_[voiceIndex] = voice_w0;
      sub_w0_[voiceIndex] = voice_w0 * 0.5f;

      const float pan = 0.5f + width * kPanSpread[voiceIndex];
      pan_left_[voiceIndex] = 1.f - pan;
      pan_right_[voiceIndex] = pan;

      energy += voice_gain_[voiceIndex] * voice_gain_[voiceIndex];
    }

    if (energy < 1e-6f)
      norm_gain_ = 0.f;
    else
      norm_gain_ = kOutputTrim / sqrtf(energy);
  }

  Params params_;
  float base_w0_ = 0.f;
  float bl_idx_ = 0.f;
  float norm_gain_ = kOutputTrim;
  float w0_[kVoiceCount] = {};
  float sub_w0_[kVoiceCount] = {};
  float saw_phase_[kVoiceCount] = {};
  float sub_phase_[kVoiceCount] = {};
  float voice_gain_[kVoiceCount] = {};
  float pan_left_[kVoiceCount] = {};
  float pan_right_[kVoiceCount] = {};
};
