#pragma once

/*
 * File: mswidth.h
 *
 * Mid/Side width. Touch snaps to Center Kill (mute Mid, keep Side).
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class MsWidth : public Processor
{
public:
  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    MID = 0U,
    SIDE,
    MIX,
    HP,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case MID:
      mid_norm_ = param_10bit_to_f32(value);
      break;
    case SIDE:
      side_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case HP:
      hp_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *) override final
  {
    side_hp_z_ = 0.f;
    kill_ = 0.f;
    pad_held_ = false;
  }

  void reset() override final
  {
    side_hp_z_ = 0.f;
    kill_ = 0.f;
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
    (void)raw;
    const float mid_gain = mid_norm_ * 2.f;
    const float side_gain = side_norm_ * 2.f;
    const float hp_c = fx::onePoleCoeff(40.f + hp_norm_ * 360.f, getSampleRate());
    const float kill_coeff = 1.f - fasterexpf(-1.f / 96.f);

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      const float live_left = in[0];
      const float live_right = in[1];
      kill_ += ((pad_held_ ? 1.f : 0.f) - kill_) * kill_coeff;

      const float mid = 0.5f * (live_left + live_right);
      float side = 0.5f * (live_left - live_right);
      side_hp_z_ += hp_c * (side - side_hp_z_);
      side = side - side_hp_z_;

      const float used_mid = mid * fx::mix(mid_gain, 0.f, kill_);
      const float used_side = side * fx::mix(side_gain, 1.35f, kill_);
      const float wet_left = used_mid + used_side;
      const float wet_right = used_mid - used_side;

      out[0] = fx::mix(live_left, wet_left, mix_);
      out[1] = fx::mix(live_right, wet_right, mix_);
      in += 2;
      out += 2;
    }
  }

private:
  float side_hp_z_ = 0.f;
  float kill_ = 0.f;
  float mid_norm_ = 0.5f;
  float side_norm_ = 0.63f;
  float hp_norm_ = 0.18f;
  float mix_ = 1.f;
  bool pad_held_ = false;
};
