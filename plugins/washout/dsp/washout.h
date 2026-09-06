#pragma once

/*
 * File: washout.h
 *
 * Wash-out blur build. Touch eases in a chorus / smear / HPF macro.
 * Lift snaps the envelope to dry immediately.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class WashOut : public Processor
{
public:
  static constexpr uint32_t kDelaySize = 24000U;

  uint32_t getBufferSize() const override final { return kDelaySize * 2U; }

  enum
  {
    BLUR = 0U,
    HPF,
    MIX,
    RISE,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case BLUR:
      blur_norm_ = param_10bit_to_f32(value);
      break;
    case HPF:
      hpf_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case RISE:
      rise_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *allocated_buffer) override final
  {
    left_ = allocated_buffer;
    right_ = allocated_buffer + kDelaySize;
    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;
    write_pos_ = 0U;
    env_ = 0.f;
    lfo_phase_ = 0.f;
    pad_held_ = false;
    hp_left_ = fx::OnePole();
    hp_right_ = fx::OnePole();
  }

  void teardown() override final
  {
    left_ = nullptr;
    right_ = nullptr;
  }

  void reset() override final
  {
    write_pos_ = 0U;
    env_ = 0.f;
    lfo_phase_ = 0.f;
    hp_left_ = fx::OnePole();
    hp_right_ = fx::OnePole();
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
    {
      pad_held_ = false;
      env_ = 0.f;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const float rise_seconds = 0.04f + (1.f - rise_norm_) * 1.1f;
    const float rise_coeff = 1.f - fasterexpf(-1.f / (rise_seconds * getSampleRate()));
    const float lfo_inc = (0.35f + blur_norm_ * 1.4f) / getSampleRate();

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      if (pad_held_)
        env_ += (1.f - env_) * rise_coeff;
      else
        env_ = 0.f;

      lfo_phase_ = fx::wrap01(lfo_phase_ + lfo_inc);
      const float lfo = fastersinfullf(lfo_phase_ * 6.283185307179586f);
      const float delay0 = 420.f + lfo * (90.f + blur_norm_ * 260.f);
      const float delay1 = 680.f - lfo * (110.f + blur_norm_ * 300.f);

      float tap0_left = 0.f;
      float tap0_right = 0.f;
      float tap1_left = 0.f;
      float tap1_right = 0.f;
      readDelay(delay0, tap0_left, tap0_right);
      readDelay(delay1, tap1_left, tap1_right);
      const float chorus_left = (tap0_left + tap1_left) * 0.5f;
      const float chorus_right = (tap0_right + tap1_right) * 0.5f;

      const float smear = blur_norm_ * env_ * 0.62f;
      left_[write_pos_] = live_left + chorus_left * smear;
      right_[write_pos_] = live_right + chorus_right * smear;
      write_pos_ = (write_pos_ + 1U) % kDelaySize;

      const float hp_hz = 80.f + env_ * hpf_norm_ * 3920.f;
      const float hp_coeff = fx::onePoleCoeff(hp_hz, getSampleRate());
      const float wet_left = hp_left_.processHp(chorus_left, hp_coeff);
      const float wet_right = hp_right_.processHp(chorus_right, hp_coeff);

      const float amount = env_ * mix_;
      out[0] = fx::mix(live_left, wet_left, amount);
      out[1] = fx::mix(live_right, wet_right, amount);
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  void readDelay(float delay_samples, float &left, float &right) const
  {
    if (delay_samples < 1.f)
      delay_samples = 1.f;
    if (delay_samples >= static_cast<float>(kDelaySize - 1U))
      delay_samples = static_cast<float>(kDelaySize - 2U);
    float pos = static_cast<float>(write_pos_) - delay_samples;
    if (pos < 0.f)
      pos += static_cast<float>(kDelaySize);
    const uint32_t index_a = static_cast<uint32_t>(pos) % kDelaySize;
    const uint32_t index_b = (index_a + 1U) % kDelaySize;
    const float frac = pos - static_cast<float>(static_cast<uint32_t>(pos));
    left = left_[index_a] + (left_[index_b] - left_[index_a]) * frac;
    right = right_[index_a] + (right_[index_b] - right_[index_a]) * frac;
  }

  float *left_ = nullptr;
  float *right_ = nullptr;
  fx::OnePole hp_left_;
  fx::OnePole hp_right_;
  uint32_t write_pos_ = 0U;
  float env_ = 0.f;
  float lfo_phase_ = 0.f;
  float blur_norm_ = 0.684f;
  float hpf_norm_ = 0.635f;
  float rise_norm_ = 0.489f;
  float mix_ = 1.f;
  bool pad_held_ = false;
};
