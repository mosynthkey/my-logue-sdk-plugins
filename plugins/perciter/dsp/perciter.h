#pragma once

/*
 * File: perciter.h
 *
 * Skin / liquid / metal percussion voice. Each touch is a trigger.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class PercIter : public Processor
{
public:
  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    PITCH = 0U,
    MORPH,
    MIX,
    FOLD,
    DEC,
    NOIS,
    MODE,
    NUM_PARAMS
  };

  enum
  {
    MODE_SKIN = 0,
    MODE_LIQ,
    MODE_METL
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case PITCH:
      pitch_norm_ = param_10bit_to_f32(value);
      break;
    case MORPH:
      morph_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case FOLD:
      fold_norm_ = param_10bit_to_f32(value);
      break;
    case DEC:
      decay_norm_ = param_10bit_to_f32(value);
      break;
    case NOIS:
      noise_norm_ = param_10bit_to_f32(value);
      break;
    case MODE:
      mode_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, 2.f));
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index != MODE)
      return nullptr;
    if (value <= 0)
      return "SKIN";
    if (value == 1)
      return "LIQ";
    return "METL";
  }

  void init(float *) override final
  {
    resetVoice();
    rng_ = 21U;
  }

  void reset() override final { resetVoice(); }

  void touchEvent(uint8_t, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    (void)x;
    (void)y;
    if (phase == k_unit_touch_phase_began)
      trigger();
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    (void)raw;
    float morph = morph_norm_;
    if (mode_ == MODE_SKIN)
      morph *= 0.45f;
    else if (mode_ == MODE_METL)
      morph = 0.55f + morph * 0.45f;

    const float decay = 0.9992f - (1.f - decay_norm_) * 0.006f;
    const float noise_decay = 0.996f - (1.f - decay_norm_) * 0.01f;
    const float fold = 1.f + fold_norm_ * 6.f;
    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      pitch_hz_ += (target_hz_ - pitch_hz_) * 0.0018f;
      body_phase_ = fx::wrap01(body_phase_ + pitch_hz_ / getSampleRate());
      fm_phase_ = fx::wrap01(fm_phase_ + (pitch_hz_ * (1.7f + morph * 3.4f)) / getSampleRate());

      const float sine = fastersinfullf(body_phase_ * 6.283185307179586f);
      const float fm = fastersinfullf((body_phase_ + fm_phase_ * morph * 0.8f) * 6.283185307179586f);
      float body = fx::mix(sine, fm, morph);
      body = fx::softclip(body * fold) * env_;

      const float noise = (fx::randomFloat(rng_) * 2.f - 1.f) * noise_env_ * noise_norm_;
      const float rattly = noise_lp_.processHp(noise, fx::onePoleCoeff(800.f + morph * 4000.f, getSampleRate()));
      const float wet = fx::softclip(body * (1.1f - morph * 0.25f) + rattly * (0.35f + morph));

      env_ *= decay;
      noise_env_ *= noise_decay;
      if (env_ < 1.0e-5f)
        env_ = 0.f;

      out[0] = fx::mix(in[0], wet, mix_);
      out[1] = fx::mix(in[1], wet, mix_);
      in += 2;
      out += 2;
    }
  }

private:
  void resetVoice()
  {
    env_ = 0.f;
    noise_env_ = 0.f;
    body_phase_ = 0.f;
    fm_phase_ = 0.f;
    pitch_hz_ = 80.f;
    target_hz_ = 80.f;
    noise_lp_ = fx::OnePole();
  }

  void trigger()
  {
    env_ = 1.f;
    noise_env_ = 1.f;
    const float start_hz = 40.f * fasterpow2f(pitch_norm_ * 4.2f);
    pitch_hz_ = start_hz * (1.8f - morph_norm_ * 0.6f);
    target_hz_ = start_hz * 0.55f;
    body_phase_ = 0.f;
  }

  fx::OnePole noise_lp_;
  float env_ = 0.f;
  float noise_env_ = 0.f;
  float body_phase_ = 0.f;
  float fm_phase_ = 0.f;
  float pitch_hz_ = 80.f;
  float target_hz_ = 80.f;
  float pitch_norm_ = 0.41f;
  float morph_norm_ = 0.2f;
  float fold_norm_ = 0.3f;
  float decay_norm_ = 0.47f;
  float noise_norm_ = 0.35f;
  float mix_ = 1.f;
  uint32_t rng_ = 21U;
  uint8_t mode_ = MODE_SKIN;
};
