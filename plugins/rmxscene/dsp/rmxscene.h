#pragma once

/*
 * File: rmxscene.h
 *
 * RMX-style Scene FX. Pad-held wet morphs Build (noise + rising HPF) into
 * Break (crush + echo). Release leaves a decaying echo instead of a hard cut.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class RmxScene : public Processor
{
public:
  static constexpr uint32_t kDelaySize = 48000U;

  uint32_t getBufferSize() const override final { return kDelaySize * 2U; }

  enum
  {
    SCENE = 0U,
    DPTH,
    MIX,
    REL,
    NOISE,
    NUM_PARAMS
  };

  enum
  {
    REL_ECHO = 0,
    REL_SNAP
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case SCENE:
      scene_norm_ = param_10bit_to_f32(value);
      break;
    case DPTH:
      depth_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case REL:
      snap_release_ = value != 0;
      break;
    case NOISE:
      noise_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index == REL)
      return (value != 0) ? "SNAP" : "ECHO";
    return nullptr;
  }

  void init(float *allocated_buffer) override final
  {
    delay_left_ = allocated_buffer;
    delay_right_ = allocated_buffer + kDelaySize;
    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;
    write_pos_ = 0U;
    pad_held_ = false;
    wet_ = 0.f;
    release_ = 0.f;
    hp_z_ = 0.f;
    lp_z_ = 0.f;
    rng_ = 99U;
    crush_hold_ = 0.f;
    crush_count_ = 0U;
    bpm_ = 120.f;
  }

  void teardown() override final
  {
    delay_left_ = nullptr;
    delay_right_ = nullptr;
  }

  void reset() override final
  {
    write_pos_ = 0U;
    wet_ = 0.f;
    release_ = 0.f;
    if (delay_left_ != nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < kDelaySize; ++sampleIndex)
      {
        delay_left_[sampleIndex] = 0.f;
        delay_right_[sampleIndex] = 0.f;
      }
    }
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 40.f && tempo < 300.f)
      bpm_ = tempo;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    const bool down = phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
                      phase == k_unit_touch_phase_stationary;
    if (!down && pad_held_)
      release_ = snap_release_ ? 0.15f : 1.f;
    pad_held_ = down;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const float build = fx::clip01((scene_norm_ - 0.5f) * 2.f);
    const float brk = 1.f - build;
    const float depth = depth_norm_;
    const uint32_t echo_samples = fx::samplesPerBeat(bpm_, getSampleRate()) / 2U;
    const uint32_t delay_read = (echo_samples < kDelaySize - 1U) ? echo_samples : (kDelaySize - 1U);
    const float wet_coeff = 1.f - fasterexpf(-1.f / 220.f);

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      const float target = pad_held_ ? 1.f : 0.f;
      wet_ += (target - wet_) * wet_coeff;
      if (!pad_held_ && release_ > 0.f)
        release_ *= snap_release_ ? 0.92f : 0.9992f;

      const float scene_amt = fx::clip01(wet_ + release_);
      const float noise = (fx::randomFloat(rng_) * 2.f - 1.f) * noise_norm_ * build * depth * scene_amt;
      float colored = 0.5f * (live_left + live_right) + noise;

      const float hp_hz = 80.f + build * depth * 4200.f * scene_amt;
      const float hp_c = fx::onePoleCoeff(hp_hz, getSampleRate());
      hp_z_ += hp_c * (colored - hp_z_);
      float built = colored - hp_z_;

      float crushed = built;
      const uint32_t hold = 1U + static_cast<uint32_t>(brk * depth * 24.f);
      if (crush_count_ == 0U)
      {
        crush_hold_ = built;
        crush_count_ = hold;
      }
      else
      {
        --crush_count_;
        crushed = crush_hold_;
      }
      const float crushed_q = fx::clip(crushed * (1.f + brk * 4.f), -1.f, 1.f);
      crushed = static_cast<float>(static_cast<int32_t>(crushed_q * (3.f + (1.f - brk * depth) * 20.f))) /
                (3.f + (1.f - brk * depth) * 20.f);

      float scene = built * build + crushed * brk;
      const uint32_t read = (write_pos_ + kDelaySize - delay_read) % kDelaySize;
      const float echo_l = delay_left_[read];
      const float echo_r = delay_right_[read];
      const float echo_fb = 0.25f + brk * depth * 0.55f + release_ * 0.35f;
      scene = scene + 0.5f * (echo_l + echo_r) * echo_fb * (0.35f + brk);

      delay_left_[write_pos_] = scene * 0.7f + live_left * 0.3f;
      delay_right_[write_pos_] = scene * 0.7f + live_right * 0.3f;
      write_pos_ = (write_pos_ + 1U) % kDelaySize;

      const float amount = fx::clip01(scene_amt) * mix_;
      out[0] = fx::mix(live_left, scene, amount);
      out[1] = fx::mix(live_right, scene, amount);
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  float *delay_left_ = nullptr;
  float *delay_right_ = nullptr;
  uint32_t write_pos_ = 0U;
  uint32_t crush_count_ = 0U;
  uint32_t rng_ = 99U;
  float crush_hold_ = 0.f;
  float wet_ = 0.f;
  float release_ = 0.f;
  float hp_z_ = 0.f;
  float lp_z_ = 0.f;
  float scene_norm_ = 0.68f;
  float depth_norm_ = 0.68f;
  float noise_norm_ = 0.5f;
  float mix_ = 1.f;
  float bpm_ = 120.f;
  bool pad_held_ = false;
  bool snap_release_ = false;
};
