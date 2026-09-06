#pragma once

/*
 * File: riddimjg.h
 *
 * Sound-system riddim juggle. Always records a loop and splits it into
 * version slices. Touch plays the selected slice; XFD crossfades changes.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class RiddimJg : public Processor
{
public:
  static constexpr uint32_t kMaxBuf = 384000U;

  uint32_t getBufferSize() const override final { return kMaxBuf * 2U; }

  enum
  {
    VER = 0U,
    XFD,
    MIX,
    SLCS,
    BARS,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case VER:
      ver_norm_ = param_10bit_to_f32(value);
      break;
    case XFD:
      xfd_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case SLCS:
      slcs_sel_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, 2.f));
      break;
    case BARS:
      bars_sel_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, 2.f));
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index == SLCS)
    {
      if (value <= 0)
        return "4";
      if (value == 1)
        return "6";
      return "8";
    }
    if (index != BARS)
      return nullptr;
    if (value <= 0)
      return "1";
    if (value == 1)
      return "2";
    return "4";
  }

  void init(float *allocated_buffer) override final
  {
    left_ = allocated_buffer;
    right_ = allocated_buffer + kMaxBuf;
    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;
    write_pos_ = 0U;
    captured_ = 0U;
    play_pos_ = 0.f;
    xfade_pos_ = 0.f;
    xfade_len_ = 64.f;
    active_slice_ = 0U;
    from_slice_ = 0U;
    playing_ = false;
    xfading_ = false;
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
    play_pos_ = 0.f;
    xfade_pos_ = 0.f;
    xfading_ = false;
    playing_ = false;
    if (left_ != nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < kMaxBuf; ++sampleIndex)
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
      playing_ = true;
      play_pos_ = 0.f;
      xfading_ = false;
      return;
    }
    if (phase == k_unit_touch_phase_moved || phase == k_unit_touch_phase_stationary)
    {
      playing_ = true;
      return;
    }
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
      playing_ = false;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const uint32_t loop_len = loopSamples();
    const uint32_t slices = sliceCount();
    uint32_t slice_len = loop_len / slices;
    if (slice_len < 8U)
      slice_len = 8U;
    const float slice_f = static_cast<float>(slice_len);
    uint32_t target = static_cast<uint32_t>(ver_norm_ * static_cast<float>(slices));
    if (target >= slices)
      target = slices - 1U;
    const float xfade_want = 64.f + xfd_norm_ * 4032.f;

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

      if (playing_ && target != active_slice_ && !xfading_)
      {
        from_slice_ = active_slice_;
        active_slice_ = target;
        xfade_len_ = xfade_want;
        xfade_pos_ = 0.f;
        xfading_ = true;
      }

      float wet_left = live_left;
      float wet_right = live_right;
      if (playing_ && loop_len > 16U && captured_ > 64U)
      {
        const uint32_t origin = (write_pos_ + kMaxBuf - loop_len) % kMaxBuf;
        readSlice(origin, active_slice_, slice_len, play_pos_, wet_left, wet_right);
        if (xfading_)
        {
          float from_left = 0.f;
          float from_right = 0.f;
          readSlice(origin, from_slice_, slice_len, play_pos_, from_left, from_right);
          const float fade = fx::clip01(xfade_pos_ / xfade_len_);
          wet_left = fx::mix(from_left, wet_left, fade);
          wet_right = fx::mix(from_right, wet_right, fade);
          xfade_pos_ += 1.f;
          if (xfade_pos_ >= xfade_len_)
            xfading_ = false;
        }

        play_pos_ += 1.f;
        if (play_pos_ >= slice_f)
          play_pos_ -= slice_f;
      }

      const float amount = playing_ ? mix_ : 0.f;
      out[0] = fx::mix(live_left, wet_left, amount);
      out[1] = fx::mix(live_right, wet_right, amount);
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  uint32_t barsCount() const
  {
    if (bars_sel_ <= 0U)
      return 1U;
    if (bars_sel_ == 1U)
      return 2U;
    return 4U;
  }

  uint32_t sliceCount() const
  {
    if (slcs_sel_ <= 0U)
      return 4U;
    if (slcs_sel_ == 1U)
      return 6U;
    return 8U;
  }

  uint32_t loopSamples() const
  {
    const float beat = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()));
    uint32_t samples = static_cast<uint32_t>(beat * 4.f * static_cast<float>(barsCount()));
    if (samples < 64U)
      samples = 64U;
    if (samples > kMaxBuf)
      samples = kMaxBuf;
    return samples;
  }

  void readSlice(uint32_t origin, uint32_t sliceIndex, uint32_t slice_len, float pos, float &left,
                 float &right) const
  {
    const float span = static_cast<float>(slice_len);
    while (pos < 0.f)
      pos += span;
    while (pos >= span)
      pos -= span;
    const uint32_t base = (origin + sliceIndex * slice_len) % kMaxBuf;
    const uint32_t index_a = (base + static_cast<uint32_t>(pos)) % kMaxBuf;
    const uint32_t index_b = (index_a + 1U) % kMaxBuf;
    const float frac = pos - static_cast<float>(static_cast<uint32_t>(pos));
    left = left_[index_a] + (left_[index_b] - left_[index_a]) * frac;
    right = right_[index_a] + (right_[index_b] - right_[index_a]) * frac;
  }

  float *left_ = nullptr;
  float *right_ = nullptr;
  uint32_t write_pos_ = 0U;
  uint32_t captured_ = 0U;
  uint32_t active_slice_ = 0U;
  uint32_t from_slice_ = 0U;
  float play_pos_ = 0.f;
  float xfade_pos_ = 0.f;
  float xfade_len_ = 64.f;
  float bpm_ = 120.f;
  float ver_norm_ = 0.f;
  float xfd_norm_ = 0.342f;
  float mix_ = 1.f;
  uint8_t slcs_sel_ = 1;
  uint8_t bars_sel_ = 1;
  bool playing_ = false;
  bool xfading_ = false;
};
