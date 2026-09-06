#pragma once

/*
 * File: dubsiren.h
 *
 * Sound-system dub siren. Touch gates a square/saw tone; a flick of PITCH
 * starts a short exponential sweep. ECHO is an internal throw delay.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class DubSiren : public Processor
{
public:
  static constexpr uint32_t kDelaySize = 24000U;

  uint32_t getBufferSize() const override final { return kDelaySize * 2U; }

  enum
  {
    PITCH = 0U,
    LFO,
    MIX,
    WAVE,
    ECHO,
    NUM_PARAMS
  };

  enum
  {
    WAVE_SQ = 0,
    WAVE_SAW
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case PITCH:
    {
      const float next = param_10bit_to_f32(value);
      if (pad_held_ && fx::absf(next - pitch_norm_) > 0.15f)
      {
        sweep_from_hz_ = currentHz();
        sweep_age_ = 0.f;
        sweeping_ = true;
      }
      pitch_norm_ = next;
      break;
    }
    case LFO:
      lfo_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case WAVE:
      wave_ = (value != 0) ? WAVE_SAW : WAVE_SQ;
      break;
    case ECHO:
      echo_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index != WAVE)
      return nullptr;
    return (value != 0) ? "SAW" : "SQ";
  }

  void init(float *allocated_buffer) override final
  {
    left_ = allocated_buffer;
    right_ = allocated_buffer + kDelaySize;
    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;
    write_pos_ = 0U;
    phase_ = 0.f;
    lfo_phase_ = 0.f;
    amp_ = 0.f;
    sweep_age_ = 0.f;
    sweep_from_hz_ = 220.f;
    sweeping_ = false;
    pad_held_ = false;
  }

  void teardown() override final
  {
    left_ = nullptr;
    right_ = nullptr;
  }

  void reset() override final
  {
    write_pos_ = 0U;
    phase_ = 0.f;
    lfo_phase_ = 0.f;
    amp_ = 0.f;
    sweeping_ = false;
    if (left_ != nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < kDelaySize; ++sampleIndex)
      {
        left_[sampleIndex] = 0.f;
        right_[sampleIndex] = 0.f;
      }
    }
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
    const float amp_coeff = 1.f - fasterexpf(-1.f / (pad_held_ ? 32.f : 180.f));
    const float lfo_hz = 0.2f + lfo_norm_ * 11.8f;
    const float lfo_inc = lfo_hz / getSampleRate();
    const float depth = lfo_norm_;
    uint32_t delay_samples = 256U + static_cast<uint32_t>(echo_norm_ * 22000.f);
    if (delay_samples >= kDelaySize)
      delay_samples = kDelaySize - 1U;
    const float echo_send = echo_norm_ * 0.72f;
    const float echo_fb = 0.28f + echo_norm_ * 0.42f;
    const float inv_sr = 1.f / getSampleRate();

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      amp_ += ((pad_held_ ? 1.f : 0.f) - amp_) * amp_coeff;

      float target_hz = 80.f + pitch_norm_ * 1120.f;
      if (sweeping_)
      {
        sweep_age_ += inv_sr;
        const float env = 1.f - fasterexpf(-sweep_age_ * 16.f);
        target_hz = sweep_from_hz_ + (target_hz - sweep_from_hz_) * env;
        if (sweep_age_ >= 0.25f)
          sweeping_ = false;
      }

      lfo_phase_ = fx::wrap01(lfo_phase_ + lfo_inc);
      const float lfo = fastersinfullf(lfo_phase_ * 6.283185307179586f);
      const float hz = target_hz * (1.f + lfo * depth * 0.38f);
      const float inc = hz / getSampleRate();
      phase_ = fx::wrap01(phase_ + inc);

      float osc = 0.f;
      if (wave_ == WAVE_SAW)
        osc = fx::blepSaw(phase_, inc);
      else
        osc = fx::blepPulse(phase_, inc, 0.5f);

      const float siren = osc * amp_ * 0.55f;
      const uint32_t read_pos = (write_pos_ + kDelaySize - delay_samples) % kDelaySize;
      const float echo_left = left_[read_pos];
      const float echo_right = right_[read_pos];
      left_[write_pos_] = siren + echo_left * echo_fb;
      right_[write_pos_] = siren + echo_right * echo_fb;
      write_pos_ = (write_pos_ + 1U) % kDelaySize;

      const float wet_left = siren + echo_left * echo_send;
      const float wet_right = siren + echo_right * echo_send;
      out[0] = fx::mix(in[0], wet_left, mix_);
      out[1] = fx::mix(in[1], wet_right, mix_);
      in += 2;
      out += 2;
    }
  }

private:
  float currentHz() const { return 80.f + pitch_norm_ * 1120.f; }

  float *left_ = nullptr;
  float *right_ = nullptr;
  uint32_t write_pos_ = 0U;
  float phase_ = 0.f;
  float lfo_phase_ = 0.f;
  float amp_ = 0.f;
  float sweep_age_ = 0.f;
  float sweep_from_hz_ = 220.f;
  float pitch_norm_ = 0.47f;
  float lfo_norm_ = 0.41f;
  float echo_norm_ = 0.27f;
  float mix_ = 1.f;
  uint8_t wave_ = WAVE_SQ;
  bool sweeping_ = false;
  bool pad_held_ = false;
};
