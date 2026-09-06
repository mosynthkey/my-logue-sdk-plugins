#pragma once

/*
 * File: sliproll.h
 *
 * DJM Slip Roll. Always records. Touch loops the current slice while the
 * live input (the "slip") keeps advancing underneath. Changing length
 * recaptures. Release is dry at the real timeline.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class SlipRoll : public Processor
{
public:
  static constexpr uint32_t kMaxBuf = 192000U;

  uint32_t getBufferSize() const override final { return kMaxBuf * 2U; }

  enum
  {
    LEN = 0U,
    SLIP,
    MIX,
    GLUE,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case LEN:
      len_norm_ = param_10bit_to_f32(value);
      break;
    case SLIP:
      slip_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case GLUE:
      glue_norm_ = param_10bit_to_f32(value);
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
    loop_start_ = 0U;
    loop_len_ = 2048U;
    loop_pos_ = 0.f;
    rolling_ = false;
    last_len_ = 0U;
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
    captured_ = 0U;
    rolling_ = false;
    loop_pos_ = 0.f;
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
      captureNow();
      rolling_ = true;
      return;
    }
    if (phase == k_unit_touch_phase_moved || phase == k_unit_touch_phase_stationary)
    {
      rolling_ = true;
      return;
    }
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
      rolling_ = false;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const uint32_t want = lengthSamples();
    if (rolling_ && want != last_len_)
      captureNow();

    const float xfade = 16.f + glue_norm_ * 240.f;

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

      float wet_left = live_left;
      float wet_right = live_right;
      if (rolling_ && loop_len_ > 8U)
      {
        readLoop(loop_pos_, wet_left, wet_right);
        const float fade = (loop_pos_ < xfade) ? (loop_pos_ / xfade) : 1.f;
        const float tail = static_cast<float>(loop_len_) - loop_pos_;
        const float fade_out = (tail < xfade) ? (tail / xfade) : 1.f;
        const float window = fade * fade_out;
        wet_left *= window;
        wet_right *= window;
        loop_pos_ += 1.f;
        if (loop_pos_ >= static_cast<float>(loop_len_))
          loop_pos_ -= static_cast<float>(loop_len_);
      }

      const float roll_amt = rolling_ ? mix_ : 0.f;
      const float dry_amt = rolling_ ? slip_norm_ : 1.f;
      out[0] = live_left * dry_amt + (wet_left - live_left * dry_amt) * roll_amt;
      out[1] = live_right * dry_amt + (wet_right - live_right * dry_amt) * roll_amt;
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  uint32_t lengthSamples() const
  {
    const float beat = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()));
    static const float kDiv[] = {16.f, 8.f, 4.f, 2.f, 1.f, 0.5f};
    const float index = len_norm_ * 5.f;
    const uint32_t lower = static_cast<uint32_t>(index);
    const uint32_t upper = (lower < 5U) ? lower + 1U : 5U;
    const float frac = index - static_cast<float>(lower);
    const float div = kDiv[lower] + (kDiv[upper] - kDiv[lower]) * frac;
    uint32_t samples = static_cast<uint32_t>(beat / div);
    if (samples < 64U)
      samples = 64U;
    if (samples > kMaxBuf / 2U)
      samples = kMaxBuf / 2U;
    return samples;
  }

  void captureNow()
  {
    loop_len_ = lengthSamples();
    last_len_ = loop_len_;
    loop_start_ = (write_pos_ + kMaxBuf - loop_len_) % kMaxBuf;
    loop_pos_ = 0.f;
  }

  void readLoop(float pos, float &left, float &right) const
  {
    const uint32_t index_a = (loop_start_ + static_cast<uint32_t>(pos)) % kMaxBuf;
    const uint32_t index_b = (index_a + 1U) % kMaxBuf;
    const float frac = pos - static_cast<float>(static_cast<uint32_t>(pos));
    left = left_[index_a] + (left_[index_b] - left_[index_a]) * frac;
    right = right_[index_a] + (right_[index_b] - right_[index_a]) * frac;
  }

  float *left_ = nullptr;
  float *right_ = nullptr;
  uint32_t write_pos_ = 0U;
  uint32_t captured_ = 0U;
  uint32_t loop_start_ = 0U;
  uint32_t loop_len_ = 2048U;
  uint32_t last_len_ = 0U;
  float loop_pos_ = 0.f;
  float bpm_ = 120.f;
  float len_norm_ = 0.3f;
  float slip_norm_ = 0.f;
  float glue_norm_ = 0.18f;
  float mix_ = 1.f;
  bool rolling_ = false;
};
