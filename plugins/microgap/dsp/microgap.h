#pragma once

/*
 * File: microgap.h
 *
 * Vacuum micro-gap before a drop. Touch arms a short mute, optionally
 * quantized to the next downbeat, with a tiny noise floor in the hole.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class MicroGap : public Processor
{
public:
  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    GAP = 0U,
    NOIS,
    MIX,
    QNT,
    NUM_PARAMS
  };

  enum
  {
    QNT_FREE = 0,
    QNT_BEAT
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case GAP:
      gap_norm_ = param_10bit_to_f32(value);
      break;
    case NOIS:
      nois_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case QNT:
      qnt_ = (value != 0) ? QNT_BEAT : QNT_FREE;
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index != QNT)
      return nullptr;
    return (value != 0) ? "BEAT" : "FREE";
  }

  void init(float *) override final
  {
    bpm_ = 120.f;
    clock_ = 0.f;
    gap_pos_ = 0.f;
    gap_len_ = 1.f;
    rng_ = 41U;
    armed_ = false;
    gapping_ = false;
    used_this_touch_ = false;
  }

  void reset() override final
  {
    clock_ = 0.f;
    gap_pos_ = 0.f;
    armed_ = false;
    gapping_ = false;
    used_this_touch_ = false;
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
      if (!used_this_touch_ && !gapping_)
      {
        if (qnt_ == QNT_BEAT)
          armed_ = true;
        else
          startGap();
        used_this_touch_ = true;
      }
      return;
    }
    if (phase == k_unit_touch_phase_moved || phase == k_unit_touch_phase_stationary)
      return;
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
      used_this_touch_ = false;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const float bar = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate())) * 4.f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      const float prev_clock = clock_;
      clock_ += 1.f;
      if (clock_ >= bar)
        clock_ -= bar;
      const bool downbeat = prev_clock > clock_;

      if (armed_ && downbeat && !gapping_)
      {
        startGap();
        armed_ = false;
      }

      if (gapping_)
      {
        gap_pos_ += 1.f;
        if (gap_pos_ >= gap_len_)
          gapping_ = false;
      }

      if (gapping_)
      {
        const float noise = (fx::randomFloat(rng_) * 2.f - 1.f) * nois_norm_ * 0.045f;
        out[0] = live_left * (1.f - mix_) + noise;
        out[1] = live_right * (1.f - mix_) + noise * 0.93f;
      }
      else
      {
        out[0] = live_left;
        out[1] = live_right;
      }

      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  void startGap()
  {
    const float seconds = 0.03f + gap_norm_ * 0.17f;
    gap_len_ = seconds * getSampleRate();
    if (gap_len_ < 32.f)
      gap_len_ = 32.f;
    gap_pos_ = 0.f;
    gapping_ = true;
  }

  float clock_ = 0.f;
  float gap_pos_ = 0.f;
  float gap_len_ = 1.f;
  float bpm_ = 120.f;
  float gap_norm_ = 0.391f;
  float nois_norm_ = 0.078f;
  float mix_ = 1.f;
  uint32_t rng_ = 41U;
  uint8_t qnt_ = QNT_BEAT;
  bool armed_ = false;
  bool gapping_ = false;
  bool used_this_touch_ = false;
};
