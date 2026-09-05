#pragma once

/*
 * File: warpsmorph.h
 *
 * Cross-mod morph: diode ring, digital XOR, comparator, mini-vocoder, folder.
 * An internal sine carrier is mixed in harder while the pad is held.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class WarpsMorph : public Processor
{
public:
  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    ALGO = 0U,
    TONE,
    MIX,
    CARR,
    DRV,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case ALGO:
      algo_norm_ = param_10bit_to_f32(value);
      break;
    case TONE:
      tone_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case CARR:
      carr_norm_ = param_10bit_to_f32(value);
      break;
    case DRV:
      drive_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *) override final
  {
    carrier_phase_ = 0.f;
    env_ = 0.f;
    touch_mix_ = 0.f;
    pad_held_ = false;
  }

  void reset() override final
  {
    carrier_phase_ = 0.f;
    env_ = 0.f;
    touch_mix_ = 0.f;
  }

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
    const float carrier_hz = 20.f * fasterpow2f(tone_norm_ * 8.f);
    const float carrier_inc = carrier_hz / getSampleRate();
    const float drive = 0.7f + drive_norm_ * 3.2f;
    const float env_atk = fx::onePoleCoeff(40.f, getSampleRate());
    const float env_rel = fx::onePoleCoeff(8.f, getSampleRate());
    const float touch_coeff = 1.f - fasterexpf(-1.f / 160.f);

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);
      const float mono = 0.5f * (live_left + live_right);

      const float carrier = fastersinfullf(carrier_phase_ * 6.283185307179586f);
      carrier_phase_ = fx::wrap01(carrier_phase_ + carrier_inc);

      const float rect = fx::absf(mono);
      env_ += ((rect > env_) ? env_atk : env_rel) * (rect - env_);

      touch_mix_ += ((pad_held_ ? 1.f : 0.f) - touch_mix_) * touch_coeff;
      const float carrier_amt = fx::clip01(carr_norm_ * 0.65f + touch_mix_ * 0.85f);
      const float modulator = fx::mix(mono, carrier, carrier_amt);

      const float ring = diodeRing(mono, modulator);
      const float xorv = digitalXor(mono, modulator);
      const float cmp = (mono > modulator) ? mono : -mono * 0.4f;
      const float voc = carrier * env_ * 2.4f;
      const float fold = foldWave(mono + modulator * 0.7f);

      const float morph = algo_norm_ * 4.f;
      float wet = 0.f;
      if (morph < 1.f)
        wet = fx::mix(ring, xorv, morph);
      else if (morph < 2.f)
        wet = fx::mix(xorv, cmp, morph - 1.f);
      else if (morph < 3.f)
        wet = fx::mix(cmp, voc, morph - 2.f);
      else
        wet = fx::mix(voc, fold, morph - 3.f);

      wet = fx::softclip(wet * drive);
      out[0] = fx::mix(live_left, wet, mix_);
      out[1] = fx::mix(live_right, wet, mix_);
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  static float diodeRing(float a, float b)
  {
    const float pos = a + b;
    const float neg = a - b;
    const float d1 = (pos > 0.f) ? pos : pos * 0.15f;
    const float d2 = (neg > 0.f) ? neg : neg * 0.15f;
    return 0.5f * (d1 - d2);
  }

  static float digitalXor(float a, float b)
  {
    const float sa = (a >= 0.f) ? 1.f : -1.f;
    const float sb = (b >= 0.f) ? 1.f : -1.f;
    return sa * sb * fx::absf(a);
  }

  static float foldWave(float x)
  {
    x = fx::clip(x, -4.f, 4.f);
    while (x > 1.f || x < -1.f)
    {
      if (x > 1.f)
        x = 2.f - x;
      if (x < -1.f)
        x = -2.f - x;
    }
    return x;
  }

  float carrier_phase_ = 0.f;
  float env_ = 0.f;
  float touch_mix_ = 0.f;
  float algo_norm_ = 0.f;
  float tone_norm_ = 0.35f;
  float carr_norm_ = 0.2f;
  float drive_norm_ = 0.4f;
  float mix_ = 0.85f;
  bool pad_held_ = false;
};
