#pragma once

/*
 * File: shepard.h
 *
 * Infinite Shepard / Risset riser. Hold to climb; release dumps amplitude.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class Shepard : public Processor
{
public:
  static constexpr uint32_t kPartials = 8U;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    RATE = 0U,
    TONE,
    MIX,
    PART,
    NOIS,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case RATE:
      rate_norm_ = param_10bit_to_f32(value);
      break;
    case TONE:
      tone_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case PART:
      part_norm_ = param_10bit_to_f32(value);
      break;
    case NOIS:
      noise_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *) override final
  {
    climb_ = 0.f;
    amp_ = 0.f;
    pad_held_ = false;
    rng_ = 3U;
    noise_z_ = 0.f;
    for (uint32_t partialIndex = 0; partialIndex < kPartials; ++partialIndex)
      phase_[partialIndex] = 0.f;
  }

  void reset() override final
  {
    climb_ = 0.f;
    amp_ = 0.f;
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
    (void)raw;
    const float climb_inc = (0.0000025f + rate_norm_ * 0.000028f);
    const float amp_atk = 1.f - fasterexpf(-1.f / 240.f);
    const float amp_rel = 1.f - fasterexpf(-1.f / 80.f);
    const float audible = 2.f + part_norm_ * 6.f;
    const float bright = tone_norm_;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      if (pad_held_)
        climb_ = fx::wrap01(climb_ + climb_inc);
      amp_ += ((pad_held_ ? 1.f : 0.f) - amp_) * (pad_held_ ? amp_atk : amp_rel);

      float acc = 0.f;
      float amp_sum = 0.f;
      for (uint32_t partialIndex = 0; partialIndex < kPartials; ++partialIndex)
      {
        if (static_cast<float>(partialIndex) > audible)
          break;
        // Raised-cosine window across octaves so the lowest fades out as the highest fades in.
        float octave = static_cast<float>(partialIndex) + climb_;
        if (octave >= static_cast<float>(kPartials))
          octave -= static_cast<float>(kPartials);
        const float window = 0.5f * (1.f + fastercosfullf((octave / static_cast<float>(kPartials) - 0.5f) * 6.283185307179586f));
        const float hz = 55.f * fasterpow2f(octave + bright * 1.2f);
        const float inc = hz / getSampleRate();
        phase_[partialIndex] = fx::wrap01(phase_[partialIndex] + inc);
        const float tone = fastersinfullf(phase_[partialIndex] * 6.283185307179586f);
        acc += tone * window;
        amp_sum += window;
      }
      if (amp_sum > 0.001f)
        acc /= amp_sum;

      const float noise = (fx::randomFloat(rng_) * 2.f - 1.f);
      noise_z_ += 0.08f * (noise - noise_z_);
      acc += (noise - noise_z_) * noise_norm_ * 0.22f;

      const float wet = fx::softclip(acc * amp_ * 1.3f);
      out[0] = fx::mix(in[0], wet, mix_);
      out[1] = fx::mix(in[1], wet, mix_);
      in += 2;
      out += 2;
    }
  }

private:
  float phase_[kPartials] = {};
  float climb_ = 0.f;
  float amp_ = 0.f;
  float noise_z_ = 0.f;
  float rate_norm_ = 0.4f;
  float tone_norm_ = 0.55f;
  float part_norm_ = 0.7f;
  float noise_norm_ = 0.22f;
  float mix_ = 1.f;
  uint32_t rng_ = 3U;
  bool pad_held_ = false;
};
