#pragma once

/*
 * File: spiral.h
 *
 * Pioneer-style spiral delay. Writes at 1x; the read head walks the delay
 * at a playback ratio that multiplies each completed repeat.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class Spiral : public Processor
{
public:
  static constexpr uint32_t kDelaySize = 96000U;

  uint32_t getBufferSize() const override final { return kDelaySize * 2U; }

  enum
  {
    NOTE = 0U,
    DRFT,
    MIX,
    FB,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case NOTE:
      note_norm_ = param_10bit_to_f32(value);
      break;
    case DRFT:
      drift_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case FB:
      fb_norm_ = param_10bit_to_f32(value);
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
    read_pos_ = 0.f;
    travel_ = 0.f;
    playback_ratio_ = 1.f;
    pad_held_ = false;
    bpm_ = 120.f;
  }

  void teardown() override final
  {
    left_ = nullptr;
    right_ = nullptr;
  }

  void reset() override final
  {
    write_pos_ = 0U;
    read_pos_ = 0.f;
    travel_ = 0.f;
    playback_ratio_ = 1.f;
    if (left_ != nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < kDelaySize; ++sampleIndex)
      {
        left_[sampleIndex] = 0.f;
        right_[sampleIndex] = 0.f;
      }
    }
  }

  void setTempo(float tempo) override final
  {
    if (tempo >= 40.f && tempo <= 300.f)
      bpm_ = tempo;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began)
    {
      pad_held_ = true;
      playback_ratio_ = 1.f;
      travel_ = 0.f;
      read_pos_ = static_cast<float>(write_pos_);
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
    const float beat = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()));
    const float beats = 0.125f + note_norm_ * 0.875f;
    uint32_t delay_samples = static_cast<uint32_t>(beat * beats);
    if (delay_samples < 64U)
      delay_samples = 64U;
    if (delay_samples >= kDelaySize)
      delay_samples = kDelaySize - 1U;

    const float drift = (drift_norm_ - 0.5f) * 0.08f;
    const float fb = 0.25f + fb_norm_ * 0.7f;
    const float period = static_cast<float>(delay_samples);

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      float echo_left = 0.f;
      float echo_right = 0.f;
      readFrac(read_pos_, echo_left, echo_right);

      const float input_left = live_left + (pad_held_ ? echo_left * fb : 0.f);
      const float input_right = live_right + (pad_held_ ? echo_right * fb : 0.f);
      left_[write_pos_] = input_left;
      right_[write_pos_] = input_right;
      write_pos_ = (write_pos_ + 1U) % kDelaySize;

      if (pad_held_)
      {
        read_pos_ -= playback_ratio_;
        if (read_pos_ < 0.f)
          read_pos_ += static_cast<float>(kDelaySize);
        travel_ += playback_ratio_;
        if (travel_ >= period)
        {
          travel_ -= period;
          playback_ratio_ *= fasterpow2f(drift);
          playback_ratio_ = fx::clip(playback_ratio_, 0.25f, 6.f);
        }
      }
      else
      {
        read_pos_ = static_cast<float>(write_pos_);
        travel_ = 0.f;
      }

      const float amount = pad_held_ ? mix_ : 0.f;
      out[0] = fx::mix(live_left, echo_left, amount);
      out[1] = fx::mix(live_right, echo_right, amount);
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  void readFrac(float pos, float &left, float &right) const
  {
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
  uint32_t write_pos_ = 0U;
  float read_pos_ = 0.f;
  float travel_ = 0.f;
  float playback_ratio_ = 1.f;
  float bpm_ = 120.f;
  float note_norm_ = 0.37f;
  float drift_norm_ = 0.68f;
  float fb_norm_ = 0.7f;
  float mix_ = 0.7f;
  bool pad_held_ = false;
};
