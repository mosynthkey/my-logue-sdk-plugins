#pragma once

/*
 * File: mohowl.h
 *
 * NTS-3 Processor wrapper for the MoHowl feedback-howl engine.
 */

#include "mohowl_engine.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class MoHowl : public Processor
{
public:
  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    PITCH = 0U,
    FEED,
    MIX,
    HARM,
    SWOOP,
    DEC,
    LEVEL,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    MoHowlEngine::Params params = engine_.getParams();
    switch (index)
    {
    case PITCH:
      params.pitch = param_10bit_to_f32(value);
      break;
    case FEED:
      params.feedback = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = value / 1000.f;
      if (mix_ < 0.f)
        mix_ = 0.f;
      if (mix_ > 1.f)
        mix_ = 1.f;
      return;
    case HARM:
      params.harmonics = param_10bit_to_f32(value);
      break;
    case SWOOP:
      params.swoop = param_10bit_to_f32(value);
      break;
    case DEC:
      params.decay = param_10bit_to_f32(value);
      break;
    case LEVEL:
      params.level = param_10bit_to_f32(value);
      break;
    default:
      return;
    }
    engine_.setParams(params);
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    (void)index;
    (void)value;
    return nullptr;
  }

  void init(float *) override final
  {
    mix_ = 1.f;
    engine_.init();
  }

  void reset() override final { engine_.reset(); }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
        phase == k_unit_touch_phase_stationary)
    {
      engine_.gate(true);
      return;
    }

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
      engine_.gate(false);
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    (void)raw;
    const float sample_rate = getSampleRate();
    const float dry_gain = 1.f - mix_;
    const float wet_gain = mix_;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      const float wet = engine_.render(sample_rate);
      out[0] = in[0] * dry_gain + wet * wet_gain;
      out[1] = in[1] * dry_gain + wet * wet_gain;
      in += 2;
      out += 2;
    }
  }

private:
  MoHowlEngine engine_;
  float mix_ = 1.f;
};
