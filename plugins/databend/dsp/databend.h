#pragma once

/*
 * File: databend.h
 *
 * Media-failure buffer: varispeed bend plus word-corruption events.
 * Touch freezes the current damaged window into a looping frame.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class DataBend : public Processor
{
public:
  static constexpr uint32_t kMaxBufSamples = 96000U;

  uint32_t getBufferSize() const override final { return kMaxBufSamples * 2U; }

  enum
  {
    BEND = 0U,
    CRUD,
    MIX,
    RATE,
    BITS,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case BEND:
      bend_norm_ = param_10bit_to_f32(value);
      break;
    case CRUD:
      crud_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case RATE:
      rate_norm_ = param_10bit_to_f32(value);
      break;
    case BITS:
      bits_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *allocated_buffer) override final
  {
    left_ = allocated_buffer;
    right_ = allocated_buffer + kMaxBufSamples;
    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;
    write_pos_ = 0U;
    play_pos_ = 0.f;
    hold_left_ = 0.f;
    hold_right_ = 0.f;
    rng_ = 7U;
    event_timer_ = 0U;
    frozen_ = false;
    freeze_start_ = 0U;
    freeze_length_ = 2048U;
    skip_hold_ = 0U;
    wet_ = 0.f;
  }

  void teardown() override final
  {
    left_ = nullptr;
    right_ = nullptr;
  }

  void reset() override final
  {
    write_pos_ = 0U;
    play_pos_ = 0.f;
    frozen_ = false;
    skip_hold_ = 0U;
    if (left_ != nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < kMaxBufSamples; ++sampleIndex)
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
      frozen_ = true;
      freeze_length_ = 256U + static_cast<uint32_t>(crud_norm_ * 4096.f);
      freeze_start_ = (write_pos_ + kMaxBufSamples - freeze_length_) % kMaxBufSamples;
      play_pos_ = static_cast<float>(freeze_start_);
      return;
    }
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
      frozen_ = false;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const float wet_coeff = 1.f - fasterexpf(-1.f / 128.f);
    const float event_period = 200.f + (1.f - rate_norm_) * 4000.f;
    const float crush_levels = 2.f + (1.f - bits_norm_) * 30.f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      if (!frozen_)
      {
        left_[write_pos_] = live_left;
        right_[write_pos_] = live_right;
        write_pos_ = (write_pos_ + 1U) % kMaxBufSamples;
      }

      if (event_timer_ == 0U)
      {
        const float fire = fx::randomFloat(rng_);
        if (fire < crud_norm_ * 0.85f + bend_norm_ * 0.15f)
        {
          skip_hold_ = 32U + static_cast<uint32_t>(fx::randomFloat(rng_) * (80.f + crud_norm_ * 800.f));
          hold_left_ = live_left;
          hold_right_ = live_right;
        }
        event_timer_ = static_cast<uint32_t>(event_period * (0.4f + fx::randomFloat(rng_) * 1.2f));
      }
      else
      {
        --event_timer_;
      }

      float rate = 1.f + (bend_norm_ - 0.5f) * 1.4f;
      rate += (fx::randomFloat(rng_) - 0.5f) * bend_norm_ * 0.35f;
      if (rate < 0.25f)
        rate = 0.25f;

      float src_left = live_left;
      float src_right = live_right;
      if (frozen_ || bend_norm_ > 0.02f || crud_norm_ > 0.02f)
      {
        if (frozen_)
        {
          play_pos_ += rate;
          const float end = static_cast<float>(freeze_start_) + static_cast<float>(freeze_length_);
          if (play_pos_ >= end)
            play_pos_ -= static_cast<float>(freeze_length_);
        }
        else
        {
          const float read = static_cast<float>(write_pos_) - 64.f - bend_norm_ * 2400.f;
          play_pos_ = read;
          if (play_pos_ < 0.f)
            play_pos_ += static_cast<float>(kMaxBufSamples);
        }

        const uint32_t index_a = static_cast<uint32_t>(play_pos_) % kMaxBufSamples;
        const uint32_t index_b = (index_a + 1U) % kMaxBufSamples;
        const float frac = play_pos_ - static_cast<float>(static_cast<uint32_t>(play_pos_));
        src_left = left_[index_a] + (left_[index_b] - left_[index_a]) * frac;
        src_right = right_[index_a] + (right_[index_b] - right_[index_a]) * frac;
      }

      if (skip_hold_ > 0U)
      {
        src_left = hold_left_;
        src_right = hold_right_;
        --skip_hold_;
      }

      if (bits_norm_ > 0.02f)
      {
        src_left = quantize(src_left, crush_levels);
        src_right = quantize(src_right, crush_levels);
      }

      if (crud_norm_ > 0.4f && ((fx::nextRandom(rng_) & 255U) < static_cast<uint32_t>(crud_norm_ * 40.f)))
      {
        const uint32_t scramble = fx::nextRandom(rng_);
        src_left = ((scramble & 1U) != 0U) ? -src_left : src_left * 0.15f;
        src_right = ((scramble & 2U) != 0U) ? -src_right : src_right * 0.15f;
      }

      const float target = (frozen_ || bend_norm_ > 0.01f || crud_norm_ > 0.01f) ? 1.f : 0.f;
      wet_ += (target - wet_) * wet_coeff;
      const float amount = wet_ * mix_;
      out[0] = fx::mix(live_left, src_left, amount);
      out[1] = fx::mix(live_right, src_right, amount);

      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  static float quantize(float sample, float levels)
  {
    const float scaled = fx::clip(sample, -1.f, 1.f) * levels;
    const float snapped = static_cast<float>(static_cast<int32_t>(scaled));
    return (levels > 0.f) ? (snapped / levels) : 0.f;
  }

  float *left_ = nullptr;
  float *right_ = nullptr;
  uint32_t write_pos_ = 0U;
  uint32_t freeze_start_ = 0U;
  uint32_t freeze_length_ = 2048U;
  uint32_t event_timer_ = 0U;
  uint32_t skip_hold_ = 0U;
  uint32_t rng_ = 7U;
  float play_pos_ = 0.f;
  float hold_left_ = 0.f;
  float hold_right_ = 0.f;
  float bend_norm_ = 0.2f;
  float crud_norm_ = 0.18f;
  float rate_norm_ = 0.4f;
  float bits_norm_ = 0.f;
  float mix_ = 1.f;
  float wet_ = 0.f;
  bool frozen_ = false;
};
