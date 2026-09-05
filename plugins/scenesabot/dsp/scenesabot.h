#pragma once

/*
 * File: scenesabot.h
 *
 * Loop-Saboteur / Octatrack Delay Ctrl. A tempo-synced 1/2/4-bar stereo ring
 * buffer keeps AUDIO IN (prefer get_raw_input). Pad up is dry. Touch arms
 * sabotage: X jumps to a 16th-step, Y grows the wrecked window from one
 * step to the whole loop. TYPE selects reverse, ratchet, crush, stretch,
 * or all of them together.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class SceneSabot : public Processor
{
public:
  static constexpr uint32_t kMaxBufSamples = 384000U;
  static constexpr float kMinBpm = 40.f;
  static constexpr float kMaxBpm = 300.f;
  static constexpr float kJumpFadeSamples = 64.f;

  uint32_t getBufferSize() const override final { return kMaxBufSamples * 2U; }

  enum
  {
    POS = 0U,
    AMT,
    MIX,
    BARS,
    TYPE,
    NUM_PARAMS
  };

  enum
  {
    TYPE_REV = 0,
    TYPE_RCHT,
    TYPE_CRSH,
    TYPE_STRC,
    TYPE_ALL
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case POS:
      pos_norm_ = param_10bit_to_f32(value);
      break;
    case AMT:
      amt_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case BARS:
      bars_sel_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, 2.f));
      break;
    case TYPE:
      type_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, 4.f));
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index == BARS)
    {
      if (value <= 0)
        return "1";
      if (value == 1)
        return "2";
      return "4";
    }
    if (index == TYPE)
    {
      if (value <= 0)
        return "REV";
      if (value == 1)
        return "RCHT";
      if (value == 2)
        return "CRSH";
      if (value == 3)
        return "STRC";
      return "ALL";
    }
    return nullptr;
  }

  void init(float *allocated_buffer) override final
  {
    left_ = allocated_buffer;
    right_ = allocated_buffer + kMaxBufSamples;
    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;
    write_pos_ = 0U;
    captured_ = 0U;
    loop_length_ = 76800U;
    bpm_ = 120.f;
    armed_ = false;
    pending_jump_ = false;
    last_step_ = 0xFFFFFFFFU;
    window_start_ = 0U;
    window_length_ = 2048U;
    window_offset_ = 0.f;
    ratchet_acc_ = 0.f;
    wet_ = 0.f;
    jump_fade_ = 0.f;
    last_wet_left_ = 0.f;
    last_wet_right_ = 0.f;
    hold_remain_ = 0U;
    held_left_ = 0.f;
    held_right_ = 0.f;
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
    armed_ = false;
    pending_jump_ = false;
    last_step_ = 0xFFFFFFFFU;
    window_offset_ = 0.f;
    ratchet_acc_ = 0.f;
    wet_ = 0.f;
    jump_fade_ = 0.f;
    last_wet_left_ = 0.f;
    last_wet_right_ = 0.f;
    hold_remain_ = 0U;
    held_left_ = 0.f;
    held_right_ = 0.f;
    if (left_ != nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < kMaxBufSamples; ++sampleIndex)
      {
        left_[sampleIndex] = 0.f;
        right_[sampleIndex] = 0.f;
      }
    }
  }

  void setTempo(float tempo) override final
  {
    if (tempo >= kMinBpm && tempo <= kMaxBpm)
      bpm_ = tempo;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
        phase == k_unit_touch_phase_stationary)
    {
      if (!armed_)
        pending_jump_ = true;
      armed_ = true;
      return;
    }
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
      armed_ = false;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    refreshLoopLength();
    const uint32_t step_count = barsFromSel(bars_sel_) * 16U;
    const float wet_coeff = 1.f - fasterexpf(-1.f / 256.f);
    const bool reverse = (type_ == TYPE_REV) || (type_ == TYPE_ALL);
    const bool ratchet = (type_ == TYPE_RCHT) || (type_ == TYPE_ALL);
    const bool crush = (type_ == TYPE_CRSH) || (type_ == TYPE_ALL);
    const bool stretch = (type_ == TYPE_STRC) || (type_ == TYPE_ALL);
    const float increment = stretch ? (1.f - amt_norm_ * 0.78f) : 1.f;
    const float crush_amt = crush ? amt_norm_ : 0.f;
    const float crush_levels = fasterpow2f(12.f - crush_amt * 8.f);
    const uint32_t hold_len = crush ? (1U + static_cast<uint32_t>(amt_norm_ * amt_norm_ * 47.f)) : 1U;
    const float sixteenth = static_cast<float>(loop_length_ / (step_count > 0U ? step_count : 1U));
    float ratchet_period = sixteenth / (1.f + amt_norm_ * 7.f);
    if (ratchet_period < 32.f)
      ratchet_period = 32.f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      if (left_ != nullptr && loop_length_ > 1U)
      {
        left_[write_pos_] = live_left;
        right_[write_pos_] = live_right;
        write_pos_ = (write_pos_ + 1U) % loop_length_;
        if (captured_ < kMaxBufSamples)
          ++captured_;
      }

      refreshWindow(step_count);

      const bool ready = captured_ >= (window_length_ + 64U);
      const float wet_target = (armed_ && ready) ? 1.f : 0.f;
      wet_ += (wet_target - wet_) * wet_coeff;

      float wet_left = live_left;
      float wet_right = live_right;
      if (ready && window_length_ > 8U && left_ != nullptr)
      {
        const float window_len_f = static_cast<float>(window_length_);
        window_offset_ += reverse ? -increment : increment;
        wrapOffset(window_offset_, window_len_f);

        if (ratchet)
        {
          ratchet_acc_ += 1.f;
          if (ratchet_acc_ >= ratchet_period)
          {
            ratchet_acc_ = 0.f;
            window_offset_ = reverse ? (window_len_f - 1.f) : 0.f;
          }
        }

        const float read_pos =
            static_cast<float>((window_start_ + static_cast<uint32_t>(window_offset_)) % loop_length_);
        readLoop(read_pos, wet_left, wet_right);

        if (crush)
        {
          wet_left = quantize(wet_left, crush_levels);
          wet_right = quantize(wet_right, crush_levels);
          if (hold_remain_ == 0U)
          {
            held_left_ = wet_left;
            held_right_ = wet_right;
            hold_remain_ = hold_len;
          }
          --hold_remain_;
          wet_left = held_left_;
          wet_right = held_right_;
        }
      }

      if (jump_fade_ > 0.f)
      {
        const float fade = jump_fade_ / kJumpFadeSamples;
        wet_left = last_wet_left_ * fade + wet_left * (1.f - fade);
        wet_right = last_wet_right_ * fade + wet_right * (1.f - fade);
        jump_fade_ -= 1.f;
      }
      last_wet_left_ = wet_left;
      last_wet_right_ = wet_right;

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
  static uint8_t barsFromSel(uint8_t sel)
  {
    if (sel <= 0U)
      return 1U;
    if (sel == 1U)
      return 2U;
    return 4U;
  }

  static float quantize(float sample, float levels)
  {
    const float scaled = sample * levels;
    return static_cast<float>(static_cast<int32_t>(scaled)) / levels;
  }

  static void wrapOffset(float &offset, float length)
  {
    if (length <= 0.f)
    {
      offset = 0.f;
      return;
    }
    while (offset >= length)
      offset -= length;
    while (offset < 0.f)
      offset += length;
  }

  void refreshLoopLength()
  {
    const uint32_t beat = fx::samplesPerBeat(bpm_, getSampleRate());
    uint32_t samples = beat * 4U * static_cast<uint32_t>(barsFromSel(bars_sel_));
    if (samples < 4096U)
      samples = 4096U;
    if (samples > kMaxBufSamples)
      samples = kMaxBufSamples;
    if (samples != loop_length_)
    {
      if (loop_length_ > 0U)
        write_pos_ %= samples;
      loop_length_ = samples;
    }
  }

  void refreshWindow(uint32_t step_count)
  {
    if (step_count == 0U || loop_length_ < 32U)
      return;

    uint32_t step = static_cast<uint32_t>(pos_norm_ * static_cast<float>(step_count));
    if (step >= step_count)
      step = step_count - 1U;

    // Smoothstep so low Y stays a one-step fill, then opens quickly toward a full-loop wreck.
    const float curve = amt_norm_ * amt_norm_ * (3.f - 2.f * amt_norm_);
    float steps_f = 1.f + curve * static_cast<float>(step_count - 1U);
    uint32_t window_steps = static_cast<uint32_t>(steps_f + 0.5f);
    if (window_steps < 1U)
      window_steps = 1U;
    if (window_steps > step_count)
      window_steps = step_count;

    const uint32_t sixteenth = loop_length_ / step_count;
    window_start_ = (step * sixteenth) % loop_length_;
    window_length_ = window_steps * sixteenth;
    if (window_length_ < 32U)
      window_length_ = 32U;
    if (window_length_ > loop_length_)
      window_length_ = loop_length_;

    if (pending_jump_ || (armed_ && step != last_step_))
    {
      pending_jump_ = false;
      window_offset_ = 0.f;
      ratchet_acc_ = 0.f;
      hold_remain_ = 0U;
      jump_fade_ = kJumpFadeSamples;
    }
    last_step_ = step;
  }

  void readLoop(float pos, float &left, float &right) const
  {
    if (left_ == nullptr || loop_length_ < 2U)
    {
      left = 0.f;
      right = 0.f;
      return;
    }
    float wrapped = pos;
    const float length_f = static_cast<float>(loop_length_);
    while (wrapped >= length_f)
      wrapped -= length_f;
    while (wrapped < 0.f)
      wrapped += length_f;
    const uint32_t index_a = static_cast<uint32_t>(wrapped);
    const uint32_t index_b = (index_a + 1U) % loop_length_;
    const float frac = wrapped - static_cast<float>(index_a);
    left = left_[index_a] + (left_[index_b] - left_[index_a]) * frac;
    right = right_[index_a] + (right_[index_b] - right_[index_a]) * frac;
  }

  float *left_ = nullptr;
  float *right_ = nullptr;
  uint32_t write_pos_ = 0U;
  uint32_t captured_ = 0U;
  uint32_t loop_length_ = 76800U;
  uint32_t window_start_ = 0U;
  uint32_t window_length_ = 2048U;
  uint32_t last_step_ = 0xFFFFFFFFU;
  uint32_t hold_remain_ = 0U;
  float window_offset_ = 0.f;
  float ratchet_acc_ = 0.f;
  float jump_fade_ = 0.f;
  float last_wet_left_ = 0.f;
  float last_wet_right_ = 0.f;
  float held_left_ = 0.f;
  float held_right_ = 0.f;
  float wet_ = 0.f;
  float bpm_ = 120.f;
  float pos_norm_ = 0.f;
  float amt_norm_ = 0.37f;
  float mix_ = 1.f;
  uint8_t bars_sel_ = 2U;
  uint8_t type_ = TYPE_ALL;
  bool armed_ = false;
  bool pending_jump_ = false;
};
