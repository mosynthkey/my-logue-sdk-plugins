#pragma once

/*
 * File: ringexcit.h
 *
 * Input-excited Karplus-Strong string plus three modal resonators.
 * Touch injects a short noise burst so the pad can pluck with no input.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class RingExcit : public Processor
{
public:
  static constexpr uint32_t kDelaySize = 4096U;
  static constexpr uint32_t kDelayMask = kDelaySize - 1U;
  static constexpr uint32_t kPartialCount = 3U;

  uint32_t getBufferSize() const override final { return kDelaySize; }

  enum
  {
    FREQ = 0U,
    TONE,
    MIX,
    STRUC,
    POS,
    NUM_PARAMS
  };

  enum
  {
    STRUC_STR = 0,
    STRUC_MOD,
    STRUC_HYB
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case FREQ:
      freq_norm_ = param_10bit_to_f32(value);
      break;
    case TONE:
      tone_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case STRUC:
      structure_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, 2.f));
      break;
    case POS:
      pos_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index != STRUC)
      return nullptr;
    if (value <= 0)
      return "STR";
    if (value == 1)
      return "MOD";
    return "HYB";
  }

  void init(float *allocated_buffer) override final
  {
    delay_ = allocated_buffer;
    for (uint32_t sampleIndex = 0; sampleIndex < kDelaySize; ++sampleIndex)
      delay_[sampleIndex] = 0.f;
    write_pos_ = 0U;
    damp_z_ = 0.f;
    noise_env_ = 0.f;
    rng_ = 11U;
    for (uint32_t partialIndex = 0; partialIndex < kPartialCount; ++partialIndex)
    {
      modal_re_[partialIndex] = 0.f;
      modal_im_[partialIndex] = 0.f;
    }
  }

  void teardown() override final { delay_ = nullptr; }

  void reset() override final
  {
    if (delay_ != nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < kDelaySize; ++sampleIndex)
        delay_[sampleIndex] = 0.f;
    }
    write_pos_ = 0U;
    damp_z_ = 0.f;
    noise_env_ = 0.f;
    for (uint32_t partialIndex = 0; partialIndex < kPartialCount; ++partialIndex)
    {
      modal_re_[partialIndex] = 0.f;
      modal_im_[partialIndex] = 0.f;
    }
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began)
      noise_env_ = 1.f;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const float hz = 40.f * fasterpow2f(freq_norm_ * 4.3f);
    float delay_samples = getSampleRate() / fx::clip(hz, 40.f, 1800.f);
    if (delay_samples < 8.f)
      delay_samples = 8.f;
    if (delay_samples > static_cast<float>(kDelaySize - 4U))
      delay_samples = static_cast<float>(kDelaySize - 4U);

    const float damp = 0.15f + (1.f - tone_norm_) * 0.75f;
    const float brightness = 0.25f + tone_norm_ * 0.75f;
    const float decay = 0.96f + tone_norm_ * 0.035f;
    const float pickup = 0.08f + pos_norm_ * 0.42f;
    const float string_mix = (structure_ == STRUC_MOD) ? 0.15f : (structure_ == STRUC_HYB ? 0.55f : 1.f);
    const float modal_mix = 1.f - string_mix * 0.65f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);
      const float excite_in = (live_left + live_right) * 0.5f;

      float burst = 0.f;
      if (noise_env_ > 0.0001f)
      {
        burst = (fx::randomFloat(rng_) * 2.f - 1.f) * noise_env_;
        noise_env_ *= 0.96f;
      }

      const float excite = excite_in * 0.85f + burst;

      const uint32_t delay_int = static_cast<uint32_t>(delay_samples);
      const float delay_frac = delay_samples - static_cast<float>(delay_int);
      const uint32_t read_a = (write_pos_ + kDelaySize - delay_int) & kDelayMask;
      const uint32_t read_b = (read_a + kDelaySize - 1U) & kDelayMask;
      float tap = delay_[read_a] + (delay_[read_b] - delay_[read_a]) * delay_frac;
      damp_z_ += (1.f - damp) * (tap - damp_z_);
      tap = damp_z_ * decay + excite;
      delay_[write_pos_] = fx::clip(tap, -4.f, 4.f);
      write_pos_ = (write_pos_ + 1U) & kDelayMask;

      const uint32_t pick_delay = 2U + static_cast<uint32_t>(pickup * delay_samples);
      const float string = delay_[(write_pos_ + kDelaySize - pick_delay) & kDelayMask];

      float modal = 0.f;
      static const float kPartialRatio[kPartialCount] = {1.f, 2.76f, 5.4f};
      for (uint32_t partialIndex = 0; partialIndex < kPartialCount; ++partialIndex)
      {
        const float omega = 6.283185307179586f * hz * kPartialRatio[partialIndex] / getSampleRate();
        const float radius = 0.9985f - (1.f - tone_norm_) * 0.01f - static_cast<float>(partialIndex) * 0.0015f;
        const float re = modal_re_[partialIndex];
        const float im = modal_im_[partialIndex];
        const float cos_w = 1.f - omega * omega * 0.5f;
        const float sin_w = omega;
        modal_re_[partialIndex] = radius * (re * cos_w - im * sin_w) + excite * (0.12f * brightness);
        modal_im_[partialIndex] = radius * (re * sin_w + im * cos_w);
        modal += modal_re_[partialIndex] * (1.f - static_cast<float>(partialIndex) * 0.22f);
      }

      const float wet = fx::softclip((string * string_mix + modal * modal_mix) * 0.7f);
      out[0] = fx::mix(live_left, wet, mix_);
      out[1] = fx::mix(live_right, wet, mix_);
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  float *delay_ = nullptr;
  uint32_t write_pos_ = 0U;
  float damp_z_ = 0.f;
  float noise_env_ = 0.f;
  float modal_re_[kPartialCount] = {};
  float modal_im_[kPartialCount] = {};
  float freq_norm_ = 0.27f;
  float tone_norm_ = 0.6f;
  float pos_norm_ = 0.4f;
  float mix_ = 0.8f;
  uint32_t rng_ = 11U;
  uint8_t structure_ = STRUC_STR;
};
