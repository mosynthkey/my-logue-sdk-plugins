#pragma once

/*
 * File: dredbass.h
 *
 * UKG / jungle suction bass. Touch gates a low oscillator through an LPF
 * whose envelope opens backward so the note feels reversed.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class DredBass : public Processor
{
public:
  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    PITCH = 0U,
    SUCK,
    MIX,
    DEC,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case PITCH:
      pitch_norm_ = param_10bit_to_f32(value);
      break;
    case SUCK:
      suck_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case DEC:
      dec_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *) override final
  {
    env_ = 0.f;
    amp_ = 0.f;
    saw_phase_ = 0.f;
    pulse_phase_ = 0.f;
    pad_held_ = false;
    lp_ = fx::OnePole();
  }

  void reset() override final
  {
    env_ = 0.f;
    amp_ = 0.f;
    lp_ = fx::OnePole();
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
    const float midi = 28.f + pitch_norm_ * 27.f;
    const float saw_inc = fx::noteToInc(midi, getSampleRate());
    const float pulse_inc = fx::noteToInc(midi + 0.07f, getSampleRate());
    const float swell = 0.06f + (1.f - dec_norm_) * 0.16f;
    const float release = 0.04f + dec_norm_ * 0.55f;
    const float atk_coeff = 1.f - fasterexpf(-1.f / (swell * getSampleRate()));
    const float rel_coeff = 1.f - fasterexpf(-1.f / (release * getSampleRate()));
    const float amp_atk = 1.f - fasterexpf(-1.f / 90.f);

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      const float env_coeff = pad_held_ ? atk_coeff : rel_coeff;
      env_ += ((pad_held_ ? 1.f : 0.f) - env_) * env_coeff;
      amp_ += ((pad_held_ ? 1.f : 0.f) - amp_) * (pad_held_ ? amp_atk : rel_coeff);

      // SUCK>0.5 inverts the mouth: start open, suck shut, then reopen.
      float mouth = env_;
      if (suck_norm_ > 0.5f)
        mouth = 1.f - 4.f * env_ * (1.f - env_);
      mouth = fx::clip01(mouth);
      const float cutoff = 80.f + mouth * mouth * (2000.f * suck_norm_ + 400.f);
      const float coeff = fx::onePoleCoeff(cutoff, getSampleRate());

      saw_phase_ = fx::wrap01(saw_phase_ + saw_inc);
      pulse_phase_ = fx::wrap01(pulse_phase_ + pulse_inc);
      const float saw = fx::blepSaw(saw_phase_, saw_inc);
      const float pulse = fx::blepPulse(pulse_phase_, pulse_inc, 0.46f);
      const float osc = saw * 0.82f + pulse * 0.18f;
      const float filtered = lp_.processLp(osc, coeff);
      const float wet = fastertanhf(filtered * amp_ * 1.55f) * 0.72f;

      out[0] = fx::mix(live_left, wet, mix_);
      out[1] = fx::mix(live_right, wet * 0.96f, mix_);
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  fx::OnePole lp_;
  float env_ = 0.f;
  float amp_ = 0.f;
  float saw_phase_ = 0.f;
  float pulse_phase_ = 0.f;
  float pitch_norm_ = 0.293f;
  float suck_norm_ = 0.606f;
  float dec_norm_ = 0.489f;
  float mix_ = 1.f;
  bool pad_held_ = false;
};
