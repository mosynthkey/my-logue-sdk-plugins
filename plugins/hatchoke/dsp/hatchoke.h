#pragma once

/*
 * File: hatchoke.h
 *
 * TR-909 open/closed hat choke. Touch triggers the open hat; closed hats
 * fire on a 16th Euclidean grid and snap the open envelope to zero.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class HatChoke : public Processor
{
public:
  static constexpr uint32_t kSteps = 16U;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    OPEN = 0U,
    CLSD,
    MIX,
    TONE,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case OPEN:
      open_norm_ = param_10bit_to_f32(value);
      break;
    case CLSD:
      clsd_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case TONE:
      tone_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *) override final
  {
    bpm_ = 120.f;
    clock_ = 0.f;
    step_index_ = 0U;
    open_env_ = 0.f;
    closed_env_ = 0.f;
    rng_ = 23U;
    pad_held_ = false;
    choked_ = false;
    touch_y_ = 512U;
    hp_open_ = fx::OnePole();
    hp_closed_ = fx::OnePole();
  }

  void reset() override final
  {
    clock_ = 0.f;
    step_index_ = 0U;
    open_env_ = 0.f;
    closed_env_ = 0.f;
    choked_ = false;
    hp_open_ = fx::OnePole();
    hp_closed_ = fx::OnePole();
  }

  void setTempo(float tempo) override final
  {
    if (tempo >= 40.f && tempo <= 300.f)
      bpm_ = tempo;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t y) override final
  {
    if (phase == k_unit_touch_phase_began)
    {
      pad_held_ = true;
      choked_ = false;
      touch_y_ = y;
      if (y < 341U)
      {
        open_env_ = 0.f;
        choked_ = true;
      }
      else
        open_env_ = 1.f;
      return;
    }
    if (phase == k_unit_touch_phase_moved || phase == k_unit_touch_phase_stationary)
    {
      pad_held_ = true;
      touch_y_ = y;
      if (y < 341U)
      {
        open_env_ = 0.f;
        choked_ = true;
      }
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
    const float sixteenth = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate())) * 0.25f;
    const uint32_t hits = static_cast<uint32_t>(clsd_norm_ * 16.f + 0.5f);
    const float hp_hz = 1800.f + tone_norm_ * 9000.f;
    const float hp_coeff = fx::onePoleCoeff(hp_hz, getSampleRate());
    const float open_dec = 0.9996f - (1.f - open_norm_) * 0.0045f;
    const float closed_dec = 0.992f - (1.f - tone_norm_) * 0.006f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      clock_ += 1.f;
      if (clock_ >= sixteenth)
      {
        clock_ -= sixteenth;
        if (fx::euclidHit(step_index_, hits, kSteps))
        {
          closed_env_ = 1.f;
          // Touch Y is not in process; closed hits always choke the open hat.
          open_env_ = 0.f;
          choked_ = true;
        }
        step_index_ = (step_index_ + 1U) % kSteps;
      }

      if (pad_held_ && !choked_ && touch_y_ >= 341U)
        open_env_ = 1.f;
      else if (pad_held_ && touch_y_ < 341U)
      {
        open_env_ = 0.f;
        choked_ = true;
      }

      const float white = fx::randomFloat(rng_) * 2.f - 1.f;
      const float open = hp_open_.processHp(white, hp_coeff) * open_env_ * 0.55f;
      const float closed = hp_closed_.processHp(white, hp_coeff) * closed_env_ * 0.42f;
      open_env_ *= open_dec;
      closed_env_ *= closed_dec;
      if (open_env_ < 0.0004f)
        open_env_ = 0.f;
      if (closed_env_ < 0.0004f)
        closed_env_ = 0.f;

      const float wet = fastertanhf((open + closed) * 1.35f);
      out[0] = fx::mix(live_left, wet, mix_);
      out[1] = fx::mix(live_right, wet * 0.97f, mix_);
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  fx::OnePole hp_open_;
  fx::OnePole hp_closed_;
  float clock_ = 0.f;
  float open_env_ = 0.f;
  float closed_env_ = 0.f;
  float bpm_ = 120.f;
  float open_norm_ = 0.508f;
  float clsd_norm_ = 0.391f;
  float tone_norm_ = 0.606f;
  float mix_ = 1.f;
  uint32_t step_index_ = 0U;
  uint32_t rng_ = 23U;
  uint32_t touch_y_ = 512U;
  bool pad_held_ = false;
  bool choked_ = false;
};
