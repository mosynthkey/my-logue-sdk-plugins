#pragma once

/*
 * File: echoout.h
 *
 * DJ Echo Out. Touch mutes dry and throws a tempo-synced delay. Release
 * keeps the wet decaying (no new input) until feedback dies.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class EchoOut : public Processor
{
public:
  static constexpr uint32_t kDelaySize = 96000U;

  uint32_t getBufferSize() const override final { return kDelaySize * 2U; }

  enum
  {
    NOTE = 0U,
    FALL,
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
    case FALL:
      fall_norm_ = param_10bit_to_f32(value);
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
    pad_held_ = false;
    throwing_ = false;
    fb_now_ = 0.f;
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
    throwing_ = false;
    fb_now_ = 0.f;
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
    if (phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
        phase == k_unit_touch_phase_stationary)
    {
      pad_held_ = true;
      throwing_ = true;
      fb_now_ = 0.72f + fb_norm_ * 0.26f;
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
    static const float kDiv[] = {8.f, 4.f, 2.f, 1.3333334f, 1.f, 0.5f};
    const float index = note_norm_ * 5.f;
    const uint32_t lower = static_cast<uint32_t>(index);
    const uint32_t upper = (lower < 5U) ? lower + 1U : 5U;
    const float frac = index - static_cast<float>(lower);
    const float div = kDiv[lower] + (kDiv[upper] - kDiv[lower]) * frac;
    uint32_t delay_samples = static_cast<uint32_t>(beat / div);
    if (delay_samples < 64U)
      delay_samples = 64U;
    if (delay_samples >= kDelaySize)
      delay_samples = kDelaySize - 1U;

    const float fall = 0.9992f - fall_norm_ * 0.0035f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      if (!pad_held_ && throwing_)
      {
        fb_now_ *= fall;
        if (fb_now_ < 0.002f)
        {
          throwing_ = false;
          fb_now_ = 0.f;
        }
      }

      const uint32_t read_pos = (write_pos_ + kDelaySize - delay_samples) % kDelaySize;
      float echo_left = left_[read_pos];
      float echo_right = right_[read_pos];
      const float input_left = pad_held_ ? live_left : 0.f;
      const float input_right = pad_held_ ? live_right : 0.f;
      left_[write_pos_] = input_left + echo_left * fb_now_;
      right_[write_pos_] = input_right + echo_right * fb_now_;
      write_pos_ = (write_pos_ + 1U) % kDelaySize;

      const float dry = throwing_ ? 0.f : 1.f;
      const float wet_amt = throwing_ ? mix_ : 0.f;
      out[0] = live_left * dry + echo_left * wet_amt;
      out[1] = live_right * dry + echo_right * wet_amt;
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  float *left_ = nullptr;
  float *right_ = nullptr;
  uint32_t write_pos_ = 0U;
  float bpm_ = 120.f;
  float note_norm_ = 0.4f;
  float fall_norm_ = 0.44f;
  float fb_norm_ = 0.8f;
  float mix_ = 1.f;
  float fb_now_ = 0.f;
  bool pad_held_ = false;
  bool throwing_ = false;
};
