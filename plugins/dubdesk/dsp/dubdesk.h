#pragma once

/*
 * File: dubdesk.h
 *
 * Dub delay desk. Stereo tempo delay with feedback through a one-pole
 * LP/HP cascade (band-pass return). Touch is a full-wet throw.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class DubDesk : public Processor
{
public:
  static constexpr uint32_t kDelaySize = 72000U;

  uint32_t getBufferSize() const override final { return kDelaySize * 2U; }

  enum
  {
    SEND = 0U,
    TONE,
    MIX,
    TIME,
    SPRD,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case SEND:
      send_norm_ = param_10bit_to_f32(value);
      break;
    case TONE:
      tone_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case TIME:
      time_norm_ = param_10bit_to_f32(value);
      break;
    case SPRD:
      sprd_norm_ = param_10bit_to_f32(value);
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
    throwing_ = false;
    bpm_ = 120.f;
    lp_left_ = fx::OnePole();
    lp_right_ = fx::OnePole();
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
    throwing_ = false;
    lp_left_ = fx::OnePole();
    lp_right_ = fx::OnePole();
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
      throwing_ = true;
      return;
    }
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
      throwing_ = false;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const float beat = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()));
    static const float kDiv[] = {8.f, 4.f, 2.f, 1.3333334f, 1.f, 0.5f};
    const float index = time_norm_ * 5.f;
    const uint32_t lower = static_cast<uint32_t>(index);
    const uint32_t upper = (lower < 5U) ? lower + 1U : 5U;
    const float frac = index - static_cast<float>(lower);
    const float div = kDiv[lower] + (kDiv[upper] - kDiv[lower]) * frac;
    uint32_t delay_left = static_cast<uint32_t>(beat / div);
    if (delay_left < 64U)
      delay_left = 64U;
    if (delay_left >= kDelaySize)
      delay_left = kDelaySize - 1U;

    uint32_t delay_right = delay_left + static_cast<uint32_t>(sprd_norm_ * static_cast<float>(delay_left) * 0.35f);
    if (delay_right >= kDelaySize)
      delay_right = kDelaySize - 1U;

    const float cutoff = 200.f + tone_norm_ * 3800.f;
    const float coeff = fx::onePoleCoeff(cutoff, getSampleRate());
    const float fb = throwing_ ? 0.96f : send_norm_ * 0.97f;
    const float wet_amt = throwing_ ? 1.f : mix_;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      const uint32_t read_left = (write_pos_ + kDelaySize - delay_left) % kDelaySize;
      const uint32_t read_right = (write_pos_ + kDelaySize - delay_right) % kDelaySize;
      const float echo_left = left_[read_left];
      const float echo_right = right_[read_right];

      const float lp_left = lp_left_.processLp(echo_left, coeff);
      const float lp_right = lp_right_.processLp(echo_right, coeff);
      const float colored_left = hp_left_.processHp(lp_left, coeff);
      const float colored_right = hp_right_.processHp(lp_right, coeff);

      left_[write_pos_] = live_left + colored_left * fb;
      right_[write_pos_] = live_right + colored_right * fb;
      write_pos_ = (write_pos_ + 1U) % kDelaySize;

      out[0] = fx::mix(live_left, colored_left, wet_amt);
      out[1] = fx::mix(live_right, colored_right, wet_amt);
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  float *left_ = nullptr;
  float *right_ = nullptr;
  fx::OnePole lp_left_;
  fx::OnePole lp_right_;
  fx::OnePole hp_left_;
  fx::OnePole hp_right_;
  uint32_t write_pos_ = 0U;
  float bpm_ = 120.f;
  float send_norm_ = 0.27f;
  float tone_norm_ = 0.41f;
  float time_norm_ = 0.49f;
  float sprd_norm_ = 0.21f;
  float mix_ = 0.45f;
  bool throwing_ = false;
};
