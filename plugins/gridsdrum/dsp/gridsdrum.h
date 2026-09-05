#pragma once

/*
 * File: gridsdrum.h
 *
 * Generative BD/SD/HH mapped like a tiny Grids. Hold to run; top-right fill.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class GridsDrum : public Processor
{
public:
  static constexpr uint32_t kSteps = 16U;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    MAPX = 0U,
    HATS,
    MIX,
    TONE,
    DEC,
    SWING,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case MAPX:
      mapx_norm_ = param_10bit_to_f32(value);
      break;
    case HATS:
      hats_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case TONE:
      tone_norm_ = param_10bit_to_f32(value);
      break;
    case DEC:
      decay_norm_ = param_10bit_to_f32(value);
      break;
    case SWING:
      swing_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *) override final
  {
    bpm_ = 120.f;
    running_ = false;
    fill_timer_ = 0U;
    clock_acc_ = 0.f;
    step_index_ = 0U;
    rng_ = 17U;
    resetVoices();
  }

  void reset() override final
  {
    running_ = false;
    fill_timer_ = 0U;
    resetVoices();
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 40.f && tempo < 300.f)
      bpm_ = tempo;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      running_ = false;
      return;
    }
    if (phase == k_unit_touch_phase_began)
    {
      running_ = true;
      step_index_ = 0U;
      clock_acc_ = 0.f;
      if (x > 760U && y > 760U)
        fill_timer_ = kSteps;
      triggerStep(0U);
      step_index_ = 1U;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    (void)raw;
    const float sixteenth = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate())) * 0.25f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      if (running_)
      {
        float step_len = sixteenth;
        if ((step_index_ & 1U) != 0U)
          step_len *= 1.f + swing_norm_ * 0.35f;
        else
          step_len *= 1.f - swing_norm_ * 0.18f;

        clock_acc_ += 1.f;
        if (clock_acc_ >= step_len)
        {
          clock_acc_ -= step_len;
          triggerStep(step_index_);
          step_index_ = (step_index_ + 1U) % kSteps;
          if (fill_timer_ > 0U)
            --fill_timer_;
        }
      }

      const float wet = renderVoices();
      out[0] = fx::mix(in[0], wet, mix_);
      out[1] = fx::mix(in[1], wet, mix_);
      in += 2;
      out += 2;
    }
  }

private:
  void resetVoices()
  {
    bd_env_ = 0.f;
    sd_env_ = 0.f;
    hh_env_ = 0.f;
    bd_phase_ = 0.f;
    sd_phase_ = 0.f;
    bd_hz_ = 60.f;
    sd_hz_ = 180.f;
  }

  float densityAt(uint32_t step, float x, float y, uint32_t voice)
  {
    // Compact Grids-like map: more X favors snare offbeats, more Y fills hats.
    const float even = ((step % 4U) == 0U) ? 1.f : 0.15f;
    const float back = ((step % 4U) == 2U) ? 1.f : 0.1f;
    const float off = ((step % 2U) == 1U) ? 1.f : 0.2f;
    if (voice == 0U)
      return fx::clip01(0.75f * even + (1.f - x) * 0.4f + ((step == 0U) ? 0.3f : 0.f));
    if (voice == 1U)
      return fx::clip01(x * back + x * 0.25f * off);
    return fx::clip01(y * 0.55f + y * off * 0.7f);
  }

  void triggerStep(uint32_t step)
  {
    const bool fill = fill_timer_ > 0U;
    const float bd_p = fill ? 0.85f : densityAt(step, mapx_norm_, hats_norm_, 0U);
    const float sd_p = fill ? 0.55f : densityAt(step, mapx_norm_, hats_norm_, 1U);
    const float hh_p = fill ? 0.95f : densityAt(step, mapx_norm_, hats_norm_, 2U);

    if (fx::randomFloat(rng_) < bd_p)
    {
      bd_env_ = 1.f;
      bd_hz_ = 48.f + tone_norm_ * 40.f;
      bd_phase_ = 0.f;
    }
    if (fx::randomFloat(rng_) < sd_p)
    {
      sd_env_ = 1.f;
      sd_hz_ = 160.f + tone_norm_ * 80.f;
      sd_phase_ = 0.f;
    }
    if (fx::randomFloat(rng_) < hh_p)
      hh_env_ = 1.f;
  }

  float renderVoices()
  {
    const float bd_dec = 0.9994f - (1.f - decay_norm_) * 0.0025f;
    const float sd_dec = 0.9988f - (1.f - decay_norm_) * 0.004f;
    const float hh_dec = 0.997f - (1.f - decay_norm_) * 0.008f;

    bd_hz_ += (42.f - bd_hz_) * 0.0015f;
    bd_phase_ = fx::wrap01(bd_phase_ + bd_hz_ / getSampleRate());
    const float bd = fastersinfullf(bd_phase_ * 6.283185307179586f) * bd_env_;
    bd_env_ *= bd_dec;

    sd_phase_ = fx::wrap01(sd_phase_ + sd_hz_ / getSampleRate());
    const float sd_tone = fastersinfullf(sd_phase_ * 6.283185307179586f);
    const float sd_noise = (fx::randomFloat(rng_) * 2.f - 1.f);
    const float sd = (sd_tone * 0.35f + sd_noise * 0.65f) * sd_env_;
    sd_env_ *= sd_dec;

    const float hh = (fx::randomFloat(rng_) * 2.f - 1.f) * hh_env_ * 0.35f;
    hh_env_ *= hh_dec;

    return fx::softclip(bd * 1.4f + sd * 0.9f + hh);
  }

  float clock_acc_ = 0.f;
  float bpm_ = 120.f;
  float mapx_norm_ = 0.3f;
  float hats_norm_ = 0.45f;
  float tone_norm_ = 0.47f;
  float decay_norm_ = 0.5f;
  float swing_norm_ = 0.f;
  float mix_ = 1.f;
  float bd_env_ = 0.f;
  float sd_env_ = 0.f;
  float hh_env_ = 0.f;
  float bd_phase_ = 0.f;
  float sd_phase_ = 0.f;
  float bd_hz_ = 60.f;
  float sd_hz_ = 180.f;
  uint32_t step_index_ = 0U;
  uint32_t fill_timer_ = 0U;
  uint32_t rng_ = 17U;
  bool running_ = false;
};
