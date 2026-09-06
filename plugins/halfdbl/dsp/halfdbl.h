#pragma once

/*
 * File: halfdbl.h
 *
 * Half / double-time bridge. Always records a tempo-synced loop. Touch plays
 * it at 0.5x / 1x / 2x. CORR blends varispeed into grain stretch.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class HalfDbl : public Processor
{
public:
  static constexpr uint32_t kMaxBuf = 384000U;
  static constexpr float kGrain = 768.f;

  uint32_t getBufferSize() const override final { return kMaxBuf * 2U; }

  enum
  {
    RATE = 0U,
    CORR,
    MIX,
    BARS,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case RATE:
      rate_norm_ = param_10bit_to_f32(value);
      break;
    case CORR:
      corr_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
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
    src_pos_ = 0.f;
    grain_phase_ = 0.f;
    playing_ = false;
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
    src_pos_ = 0.f;
    grain_phase_ = 0.f;
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
      src_pos_ = 0.f;
      grain_phase_ = 0.f;
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
    const float loop_f = static_cast<float>(loop_len);
    float rate = 1.f;
    if (rate_norm_ < 0.33f)
      rate = 0.5f;
    else if (rate_norm_ > 0.66f)
      rate = 2.f;
    const float hop = kGrain * rate;

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
      if (playing_ && loop_len > 8U && captured_ > 64U)
      {
        const uint32_t origin = (write_pos_ + kMaxBuf - loop_len) % kMaxBuf;
        float vs_left = 0.f;
        float vs_right = 0.f;
        readLoop(origin, play_pos_, loop_len, vs_left, vs_right);

        float g0_left = 0.f;
        float g0_right = 0.f;
        float g1_left = 0.f;
        float g1_right = 0.f;
        float phase1 = grain_phase_ + kGrain * 0.5f;
        if (phase1 >= kGrain)
          phase1 -= kGrain;
        readLoop(origin, src_pos_ + grain_phase_, loop_len, g0_left, g0_right);
        readLoop(origin, src_pos_ + phase1, loop_len, g1_left, g1_right);
        const float w0 = (grain_phase_ < kGrain * 0.5f) ? (grain_phase_ / (kGrain * 0.5f))
                                                       : ((kGrain - grain_phase_) / (kGrain * 0.5f));
        const float w1 = (phase1 < kGrain * 0.5f) ? (phase1 / (kGrain * 0.5f))
                                                 : ((kGrain - phase1) / (kGrain * 0.5f));
        const float gr_left = g0_left * w0 + g1_left * w1;
        const float gr_right = g0_right * w0 + g1_right * w1;

        wet_left = fx::mix(vs_left, gr_left, corr_norm_);
        wet_right = fx::mix(vs_right, gr_right, corr_norm_);

        play_pos_ += rate;
        if (play_pos_ >= loop_f)
          play_pos_ -= loop_f;
        if (play_pos_ < 0.f)
          play_pos_ += loop_f;

        grain_phase_ += 1.f;
        if (grain_phase_ >= kGrain)
        {
          grain_phase_ -= kGrain;
          src_pos_ += hop;
        }
        if (src_pos_ >= loop_f)
          src_pos_ -= loop_f;
        if (src_pos_ < 0.f)
          src_pos_ += loop_f;
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

  void readLoop(uint32_t origin, float pos, uint32_t loop_len, float &left, float &right) const
  {
    const float span = static_cast<float>(loop_len);
    while (pos < 0.f)
      pos += span;
    while (pos >= span)
      pos -= span;
    const uint32_t index_a = (origin + static_cast<uint32_t>(pos)) % kMaxBuf;
    const uint32_t index_b = (index_a + 1U) % kMaxBuf;
    const float frac = pos - static_cast<float>(static_cast<uint32_t>(pos));
    left = left_[index_a] + (left_[index_b] - left_[index_a]) * frac;
    right = right_[index_a] + (right_[index_b] - right_[index_a]) * frac;
  }

  float *left_ = nullptr;
  float *right_ = nullptr;
  uint32_t write_pos_ = 0U;
  uint32_t captured_ = 0U;
  float play_pos_ = 0.f;
  float src_pos_ = 0.f;
  float grain_phase_ = 0.f;
  float bpm_ = 120.f;
  float rate_norm_ = 0.5f;
  float corr_norm_ = 0.977f;
  float mix_ = 1.f;
  uint8_t bars_sel_ = 1;
  bool playing_ = false;
};
