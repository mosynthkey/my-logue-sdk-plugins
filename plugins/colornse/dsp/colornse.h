#pragma once

/*
 * File: colornse.h
 *
 * DJM Color FX Noise. Touch gates white / pink / brown through a stereo
 * filter that morphs high-pass (left) to low-pass (right).
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class ColorNse : public Processor
{
public:
  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    FILT = 0U,
    COLR,
    MIX,
    CUT,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case FILT:
      filt_norm_ = param_10bit_to_f32(value);
      break;
    case COLR:
      color_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case CUT:
      cut_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *) override final
  {
    amp_ = 0.f;
    pink_z_ = 0.f;
    brown_z_ = 0.f;
    rng_ = 17U;
    pad_held_ = false;
    hp_left_ = fx::OnePole();
    hp_right_ = fx::OnePole();
    lp_left_ = fx::OnePole();
    lp_right_ = fx::OnePole();
  }

  void reset() override final
  {
    amp_ = 0.f;
    pink_z_ = 0.f;
    brown_z_ = 0.f;
    hp_left_ = fx::OnePole();
    hp_right_ = fx::OnePole();
    lp_left_ = fx::OnePole();
    lp_right_ = fx::OnePole();
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
        phase == k_unit_touch_phase_stationary)
    {
      pad_held_ = true;
      return;
    }
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
      pad_held_ = false;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    (void)raw;
    const float amp_coeff = 1.f - fasterexpf(-1.f / (pad_held_ ? 48.f : 220.f));
    const float cutoff = 200.f + cut_norm_ * 7800.f;
    const float coeff = fx::onePoleCoeff(cutoff, getSampleRate());
    const float drive = 1.f + color_norm_ * 3.2f;
    const float left_morph = fx::clip01(filt_norm_ * 0.82f);
    const float right_morph = fx::clip01(filt_norm_ * 0.82f + 0.18f);

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      amp_ += ((pad_held_ ? 1.f : 0.f) - amp_) * amp_coeff;

      const float white = fx::randomFloat(rng_) * 2.f - 1.f;
      const float pink = white + 0.75f * pink_z_;
      pink_z_ = pink;
      brown_z_ += 0.02f * (white - brown_z_);

      float colored = white;
      if (color_norm_ < 0.5f)
        colored = fx::mix(white, pink * 0.28f, color_norm_ * 2.f);
      else
        colored = fx::mix(pink * 0.28f, brown_z_ * 2.4f, (color_norm_ - 0.5f) * 2.f);

      const float driven = fastertanhf(colored * drive) * amp_;
      const float hp_left = hp_left_.processHp(driven, coeff);
      const float hp_right = hp_right_.processHp(driven, coeff);
      const float lp_left = lp_left_.processLp(driven, coeff);
      const float lp_right = lp_right_.processLp(driven, coeff);

      const float wet_left = fx::mix(hp_left, lp_left, left_morph);
      const float wet_right = fx::mix(hp_right, lp_right, right_morph);
      out[0] = fx::mix(in[0], wet_left, mix_);
      out[1] = fx::mix(in[1], wet_right, mix_);
      in += 2;
      out += 2;
    }
  }

private:
  fx::OnePole hp_left_;
  fx::OnePole hp_right_;
  fx::OnePole lp_left_;
  fx::OnePole lp_right_;
  float amp_ = 0.f;
  float pink_z_ = 0.f;
  float brown_z_ = 0.f;
  float filt_norm_ = 0.68f;
  float color_norm_ = 0.34f;
  float cut_norm_ = 0.59f;
  float mix_ = 1.f;
  uint32_t rng_ = 17U;
  bool pad_held_ = false;
};
