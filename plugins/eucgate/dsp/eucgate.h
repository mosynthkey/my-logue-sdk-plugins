#pragma once

/*
 * File: eucgate.h
 *
 * Euclidean / probability transform gate. Pad-held wet: closed steps mute
 * the input on a tempo grid. Dry passthrough when the pad is up.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class EucGate : public Processor
{
public:
  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    HITS = 0U,
    DUTY,
    MIX,
    STEPS,
    ROT,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case HITS:
      hits_norm_ = param_10bit_to_f32(value);
      break;
    case DUTY:
      duty_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case STEPS:
      steps_sel_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, 2.f));
      break;
    case ROT:
      rot_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index != STEPS)
      return nullptr;
    if (value <= 0)
      return "8";
    if (value == 1)
      return "12";
    return "16";
  }

  void init(float *) override final
  {
    bpm_ = 120.f;
    clock_acc_ = 0.f;
    step_index_ = 0U;
    gate_ = 1.f;
    wet_ = 0.f;
    pad_held_ = false;
    rng_ = 5U;
    samples_into_step_ = 0.f;
  }

  void reset() override final
  {
    clock_acc_ = 0.f;
    step_index_ = 0U;
    gate_ = 1.f;
    wet_ = 0.f;
    pad_held_ = false;
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 40.f && tempo < 300.f)
      bpm_ = tempo;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    pad_held_ = phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
                phase == k_unit_touch_phase_stationary;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const uint32_t steps = (steps_sel_ == 0) ? 8U : (steps_sel_ == 1 ? 12U : 16U);
    const uint32_t hits = 1U + static_cast<uint32_t>(hits_norm_ * static_cast<float>(steps - 1U));
    const uint32_t rotate = static_cast<uint32_t>(rot_norm_ * static_cast<float>(steps));
    const float step_samples = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate())) * 4.f / static_cast<float>(steps);
    const float duty = 0.08f + duty_norm_ * 0.9f;
    const float smooth = 1.f - fasterexpf(-1.f / 48.f);
    const float wet_coeff = 1.f - fasterexpf(-1.f / 96.f);

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      wet_ += ((pad_held_ ? 1.f : 0.f) - wet_) * wet_coeff;

      clock_acc_ += 1.f;
      samples_into_step_ += 1.f;
      if (clock_acc_ >= step_samples)
      {
        clock_acc_ -= step_samples;
        samples_into_step_ = 0.f;
        step_index_ = (step_index_ + 1U) % steps;
        const uint32_t rotated = (step_index_ + rotate) % steps;
        bool hit = fx::euclidHit(rotated, hits, steps);
        if (hit && duty_norm_ < 0.95f && fx::randomFloat(rng_) > 0.35f + duty_norm_ * 0.65f)
          hit = false;
        step_open_ = hit;
      }

      const float phase = (step_samples > 1.f) ? (samples_into_step_ / step_samples) : 0.f;
      const bool inside = step_open_ && (phase < duty);
      const float target = inside ? 1.f : 0.f;
      gate_ += (target - gate_) * smooth;

      const float ducked = fx::mix(1.f, gate_, wet_ * mix_);
      out[0] = live_left * ducked;
      out[1] = live_right * ducked;
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  float clock_acc_ = 0.f;
  float samples_into_step_ = 0.f;
  float bpm_ = 120.f;
  float hits_norm_ = 0.4f;
  float duty_norm_ = 0.55f;
  float rot_norm_ = 0.f;
  float mix_ = 1.f;
  float gate_ = 1.f;
  float wet_ = 0.f;
  uint32_t step_index_ = 0U;
  uint32_t rng_ = 5U;
  uint8_t steps_sel_ = 2;
  bool pad_held_ = false;
  bool step_open_ = true;
};
