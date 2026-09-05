#pragma once

/*
 * File: talkform.h
 *
 * Dual-formant talk filter. X/Y sweep F1/F2; touch or DIGI snaps to a
 * five-vowel grid (a i u e o).
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class TalkForm : public Processor
{
public:
  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    F1 = 0U,
    F2,
    MIX,
    Q,
    DIGI,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case F1:
      f1_norm_ = param_10bit_to_f32(value);
      break;
    case F2:
      f2_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case Q:
      q_norm_ = param_10bit_to_f32(value);
      break;
    case DIGI:
      digi_ = value != 0;
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index == DIGI)
      return (value != 0) ? "DIGI" : "FREE";
    return nullptr;
  }

  void init(float *) override final
  {
    resetFilters();
    pad_held_ = false;
  }

  void reset() override final { resetFilters(); }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    pad_held_ = phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
                phase == k_unit_touch_phase_stationary;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    float f1_hz = 270.f + f1_norm_ * 730.f;
    float f2_hz = 700.f + f2_norm_ * 1700.f;
    if (digi_ || pad_held_)
      nearestVowel(f1_hz, f2_hz);

    const float q = 2.5f + q_norm_ * 10.f;
    setBandpass(bp1_, f1_hz, q);
    setBandpass(bp2_, f2_hz, q * 0.85f);

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);
      const float mono = 0.5f * (live_left + live_right);
      const float vowel = processBandpass(bp1_, mono) + processBandpass(bp2_, mono);
      const float wet = fx::softclip(vowel * 1.6f);
      out[0] = fx::mix(live_left, wet, mix_);
      out[1] = fx::mix(live_right, wet, mix_);
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  struct Biquad
  {
    float b0 = 0.f;
    float b1 = 0.f;
    float b2 = 0.f;
    float a1 = 0.f;
    float a2 = 0.f;
    float z1 = 0.f;
    float z2 = 0.f;
  };

  static void nearestVowel(float &f1, float &f2)
  {
    static const float kF1[] = {800.f, 270.f, 300.f, 530.f, 570.f};
    static const float kF2[] = {1200.f, 2300.f, 870.f, 1850.f, 840.f};
    uint32_t best = 0U;
    float best_err = 1.0e12f;
    for (uint32_t vowelIndex = 0; vowelIndex < 5U; ++vowelIndex)
    {
      const float err1 = f1 - kF1[vowelIndex];
      const float err2 = f2 - kF2[vowelIndex];
      const float err = err1 * err1 + err2 * err2;
      if (err < best_err)
      {
        best_err = err;
        best = vowelIndex;
      }
    }
    f1 = kF1[best];
    f2 = kF2[best];
  }

  static void setBandpass(Biquad &f, float hz, float q)
  {
    const float w = 6.283185307179586f * fx::clip(hz, 80.f, 6000.f) / 48000.f;
    const float cosw = fastercosfullf(w);
    const float sinw = fastersinfullf(w);
    const float alpha = sinw / (2.f * fx::clip(q, 0.4f, 18.f));
    const float a0 = 1.f + alpha;
    f.b0 = alpha / a0;
    f.b1 = 0.f;
    f.b2 = -alpha / a0;
    f.a1 = -2.f * cosw / a0;
    f.a2 = (1.f - alpha) / a0;
  }

  static float processBandpass(Biquad &f, float input)
  {
    const float output = f.b0 * input + f.z1;
    f.z1 = f.b1 * input - f.a1 * output + f.z2;
    f.z2 = f.b2 * input - f.a2 * output;
    return output;
  }

  void resetFilters()
  {
    bp1_ = Biquad();
    bp2_ = Biquad();
  }

  Biquad bp1_;
  Biquad bp2_;
  float f1_norm_ = 0.4f;
  float f2_norm_ = 0.6f;
  float q_norm_ = 0.63f;
  float mix_ = 1.f;
  bool digi_ = false;
  bool pad_held_ = false;
};
