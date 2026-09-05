#pragma once

/*
 * File: beatrepeat.h
 *
 * Clock-synced Beat Repeat. A stereo ring buffer keeps AUDIO IN (prefer
 * get_raw_input). On each 16th the engine may grab a slice and loop it.
 * Touch forces the freeze regardless of probability.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class BeatRepeat : public Processor
{
public:
  static constexpr uint32_t kMaxBufSamples = 192000U;
  static constexpr uint32_t kXfadeSamples = 64U;
  static constexpr float kMinBpm = 40.f;
  static constexpr float kMaxBpm = 300.f;

  uint32_t getBufferSize() const override final { return kMaxBufSamples * 2U; }

  enum
  {
    LEN = 0U,
    PROB,
    MIX,
    SLICE,
    FEED,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case LEN:
      len_norm_ = param_10bit_to_f32(value);
      break;
    case PROB:
      prob_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case SLICE:
      slice_norm_ = param_10bit_to_f32(value);
      break;
    case FEED:
      feed_norm_ = param_10bit_to_f32(value);
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
    captured_ = 0U;
    bpm_ = 120.f;
    rng_ = 1U;
    repeating_ = false;
    force_hold_ = false;
    wet_ = 0.f;
    loop_pos_ = 0.f;
    clock_acc_ = 0.f;
    feedback_left_ = 0.f;
    feedback_right_ = 0.f;
    loop_length_ = 2048U;
    loop_start_ = 0U;
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
    repeating_ = false;
    wet_ = 0.f;
    loop_pos_ = 0.f;
    feedback_left_ = 0.f;
    feedback_right_ = 0.f;
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
      if (!force_hold_)
        armRepeat(true);
      force_hold_ = true;
      return;
    }
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
      force_hold_ = false;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const float beat_samples = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()));
    const float sixteenth = beat_samples * 0.25f;
    const float wet_coeff = 1.f - fasterexpf(-1.f / 256.f);

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      left_[write_pos_] = live_left;
      right_[write_pos_] = live_right;
      write_pos_ = (write_pos_ + 1U) % kMaxBufSamples;
      if (captured_ < kMaxBufSamples)
        ++captured_;

      clock_acc_ += 1.f;
      if (clock_acc_ >= sixteenth)
      {
        clock_acc_ -= sixteenth;
        onSixteenth(beat_samples);
      }

      if (!force_hold_ && !repeating_)
      {
        wet_ += (0.f - wet_) * wet_coeff;
      }
      else
      {
        wet_ += (1.f - wet_) * wet_coeff;
      }

      float wet_left = live_left;
      float wet_right = live_right;
      if (repeating_ && loop_length_ > 8U)
      {
        const uint32_t index_a = loop_start_ + static_cast<uint32_t>(loop_pos_);
        const uint32_t safe_a = index_a % kMaxBufSamples;
        const uint32_t safe_b = (safe_a + 1U) % kMaxBufSamples;
        const float frac = loop_pos_ - static_cast<float>(static_cast<uint32_t>(loop_pos_));
        wet_left = left_[safe_a] + (left_[safe_b] - left_[safe_a]) * frac;
        wet_right = right_[safe_a] + (right_[safe_b] - right_[safe_a]) * frac;
        wet_left += feedback_left_ * feed_norm_ * 0.85f;
        wet_right += feedback_right_ * feed_norm_ * 0.85f;
        feedback_left_ = wet_left;
        feedback_right_ = wet_right;

        loop_pos_ += 1.f;
        if (loop_pos_ >= static_cast<float>(loop_length_))
          loop_pos_ -= static_cast<float>(loop_length_);
      }

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
  static uint32_t lengthFromNorm(float norm, float beat_samples)
  {
    static const float kDiv[] = {32.f, 16.f, 8.f, 4.f, 2.f, 1.f, 0.5f};
    const float index = fx::clip01(norm) * 6.f;
    const uint32_t lower = static_cast<uint32_t>(index);
    const uint32_t upper = (lower < 6U) ? lower + 1U : 6U;
    const float frac = index - static_cast<float>(lower);
    const float div = kDiv[lower] + (kDiv[upper] - kDiv[lower]) * frac;
    uint32_t samples = static_cast<uint32_t>(beat_samples / div);
    if (samples < 64U)
      samples = 64U;
    if (samples > kMaxBufSamples / 2U)
      samples = kMaxBufSamples / 2U;
    return samples;
  }

  void armRepeat(bool force)
  {
    const float beat_samples = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()));
    loop_length_ = lengthFromNorm(len_norm_, beat_samples);
    if (captured_ < loop_length_ + 64U)
    {
      repeating_ = force;
      return;
    }

    const uint32_t available = (captured_ > beat_samples) ? static_cast<uint32_t>(beat_samples) : captured_;
    const uint32_t slice_span = (available > loop_length_) ? (available - loop_length_) : 0U;
    const uint32_t slice_offset = static_cast<uint32_t>(slice_norm_ * static_cast<float>(slice_span));
    const uint32_t origin = (write_pos_ + kMaxBufSamples - available) % kMaxBufSamples;
    loop_start_ = (origin + slice_offset) % kMaxBufSamples;
    loop_pos_ = 0.f;
    repeating_ = true;
  }

  void onSixteenth(float beat_samples)
  {
    (void)beat_samples;
    if (force_hold_)
    {
      if (!repeating_)
        armRepeat(true);
      return;
    }

    const float roll = fx::randomFloat(rng_);
    if (roll < prob_norm_ * prob_norm_)
    {
      armRepeat(false);
      return;
    }

    // High probability stays latched; low probability releases after a miss.
    if (prob_norm_ < 0.85f)
      repeating_ = false;
  }

  float *left_ = nullptr;
  float *right_ = nullptr;
  uint32_t write_pos_ = 0U;
  uint32_t captured_ = 0U;
  uint32_t loop_start_ = 0U;
  uint32_t loop_length_ = 2048U;
  float loop_pos_ = 0.f;
  float clock_acc_ = 0.f;
  float bpm_ = 120.f;
  float len_norm_ = 0.4f;
  float prob_norm_ = 0.68f;
  float slice_norm_ = 0.f;
  float feed_norm_ = 0.2f;
  float mix_ = 1.f;
  float wet_ = 0.f;
  float feedback_left_ = 0.f;
  float feedback_right_ = 0.f;
  uint32_t rng_ = 1U;
  bool repeating_ = false;
  bool force_hold_ = false;
};
