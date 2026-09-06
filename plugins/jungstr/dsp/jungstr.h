#pragma once

/*
 * File: jungstr.h
 *
 * Akai S-series jungle vocal stretch. Always records. Touch plays two
 * overlapping grains of recent audio at an extreme stretch ratio.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class JungStr : public Processor
{
public:
  static constexpr uint32_t kMaxBuf = 192000U;

  uint32_t getBufferSize() const override final { return kMaxBuf * 2U; }

  enum
  {
    STRC = 0U,
    GRAIN,
    MIX,
    HOLD,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case STRC:
      strc_norm_ = param_10bit_to_f32(value);
      break;
    case GRAIN:
      grain_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case HOLD:
      hold_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *allocated_buffer) override final
  {
    left_ = allocated_buffer;
    right_ = allocated_buffer + kMaxBuf;
    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;
    write_pos_ = 0U;
    captured_ = 0U;
    capture_end_ = 0U;
    src_pos_ = 0.f;
    grain_phase_ = 0.f;
    wet_ = 0.f;
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
    captured_ = 0U;
    src_pos_ = 0.f;
    grain_phase_ = 0.f;
    wet_ = 0.f;
    if (left_ != nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < kMaxBuf; ++sampleIndex)
      {
        left_[sampleIndex] = 0.f;
        right_[sampleIndex] = 0.f;
      }
    }
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began)
    {
      pad_held_ = true;
      capture_end_ = write_pos_;
      src_pos_ = 0.f;
      grain_phase_ = 0.f;
      return;
    }
    if (phase == k_unit_touch_phase_moved || phase == k_unit_touch_phase_stationary)
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
    const float stretch = 1.2f + strc_norm_ * 14.8f;
    float grain_samples = (0.008f + grain_norm_ * 0.072f) * getSampleRate();
    if (grain_samples < 32.f)
      grain_samples = 32.f;
    const float src_inc = 1.f / stretch;
    const float wet_coeff = 1.f - fasterexpf(-1.f / 160.f);
    uint32_t window = captured_;
    if (window > 72000U)
      window = 72000U;
    if (window < 256U)
      window = 256U;
    const float window_f = static_cast<float>(window);

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);
      left_[write_pos_] = live_left;
      right_[write_pos_] = live_right;
      write_pos_ = (write_pos_ + 1U) % kMaxBuf;
      if (captured_ < kMaxBuf)
        ++captured_;

      if (hold_norm_ < 0.2f)
        capture_end_ = write_pos_;

      float wet_left = live_left;
      float wet_right = live_right;
      if (pad_held_ && captured_ > 64U)
      {
        const float phase0 = grain_phase_;
        float phase1 = grain_phase_ + 0.5f;
        if (phase1 >= 1.f)
          phase1 -= 1.f;
        const float w0 = (phase0 < 0.5f) ? (phase0 * 2.f) : ((1.f - phase0) * 2.f);
        const float w1 = (phase1 < 0.5f) ? (phase1 * 2.f) : ((1.f - phase1) * 2.f);

        float g0_left = 0.f;
        float g0_right = 0.f;
        float g1_left = 0.f;
        float g1_right = 0.f;
        readWindow(src_pos_ + phase0 * grain_samples, window, g0_left, g0_right);
        readWindow(src_pos_ + phase1 * grain_samples, window, g1_left, g1_right);
        wet_left = g0_left * w0 + g1_left * w1;
        wet_right = g0_right * w0 + g1_right * w1;

        src_pos_ += src_inc;
        if (src_pos_ >= window_f)
          src_pos_ -= window_f;
        grain_phase_ += 1.f / grain_samples;
        if (grain_phase_ >= 1.f)
          grain_phase_ -= 1.f;
      }

      wet_ += ((pad_held_ ? 1.f : 0.f) - wet_) * wet_coeff;
      const float amount = wet_ * mix_;
      out[0] = fx::mix(live_left, wet_left, amount);
      out[1] = fx::mix(live_right, wet_right, amount);
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  void readWindow(float offset, uint32_t window, float &left, float &right) const
  {
    const float span = static_cast<float>(window);
    while (offset < 0.f)
      offset += span;
    while (offset >= span)
      offset -= span;
    const uint32_t origin = (capture_end_ + kMaxBuf - window) % kMaxBuf;
    const uint32_t index_a = (origin + static_cast<uint32_t>(offset)) % kMaxBuf;
    const uint32_t index_b = (index_a + 1U) % kMaxBuf;
    const float frac = offset - static_cast<float>(static_cast<uint32_t>(offset));
    left = left_[index_a] + (left_[index_b] - left_[index_a]) * frac;
    right = right_[index_a] + (right_[index_b] - right_[index_a]) * frac;
  }

  float *left_ = nullptr;
  float *right_ = nullptr;
  uint32_t write_pos_ = 0U;
  uint32_t captured_ = 0U;
  uint32_t capture_end_ = 0U;
  float src_pos_ = 0.f;
  float grain_phase_ = 0.f;
  float wet_ = 0.f;
  float strc_norm_ = 0.76f;
  float grain_norm_ = 0.41f;
  float hold_norm_ = 0.2f;
  float mix_ = 1.f;
  bool pad_held_ = false;
};
