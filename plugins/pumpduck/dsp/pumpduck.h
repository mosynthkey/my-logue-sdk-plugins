#pragma once

/*
 * File: pumpduck.h
 *
 * Envelope-follower sidechain. The detector prefers raw AUDIO IN so a dry
 * kick can duck a wet pad even when unit_render is muted. Touch cycles AMP /
 * filter / internal plate destinations.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class PumpDuck : public Processor
{
public:
  static constexpr uint32_t kCombSize = 2048U;
  static constexpr uint32_t kCombMask = kCombSize - 1U;
  static constexpr uint32_t kAllpassSize = 512U;
  static constexpr uint32_t kAllpassMask = kAllpassSize - 1U;

  uint32_t getBufferSize() const override final { return kCombSize * 2U + kAllpassSize * 2U; }

  enum
  {
    TIME = 0U,
    DPTH,
    MIX,
    DEST,
    HPF,
    NUM_PARAMS
  };

  enum
  {
    DEST_AMP = 0,
    DEST_FILT,
    DEST_PLATE
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case TIME:
      time_norm_ = param_10bit_to_f32(value);
      break;
    case DPTH:
      depth_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case DEST:
      dest_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, 2.f));
      break;
    case HPF:
      hpf_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index != DEST)
      return nullptr;
    if (value <= 0)
      return "AMP";
    if (value == 1)
      return "FILT";
    return "PLAT";
  }

  void init(float *allocated_buffer) override final
  {
    comb_a_ = allocated_buffer;
    comb_b_ = allocated_buffer + kCombSize;
    ap_a_ = allocated_buffer + kCombSize * 2U;
    ap_b_ = allocated_buffer + kCombSize * 2U + kAllpassSize;
    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;
    write_pos_ = 0U;
    detect_hp_ = fx::OnePole();
    lp_ = fx::OnePole();
    follower_ = fx::EnvelopeFollower();
    duck_ = 1.f;
    pad_was_down_ = false;
  }

  void teardown() override final
  {
    comb_a_ = nullptr;
    comb_b_ = nullptr;
    ap_a_ = nullptr;
    ap_b_ = nullptr;
  }

  void reset() override final
  {
    if (comb_a_ != nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
        comb_a_[sampleIndex] = 0.f;
    }
    write_pos_ = 0U;
    detect_hp_ = fx::OnePole();
    lp_ = fx::OnePole();
    follower_ = fx::EnvelopeFollower();
    duck_ = 1.f;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began && !pad_was_down_)
    {
      dest_ = static_cast<uint8_t>((dest_ + 1U) % 3U);
      pad_was_down_ = true;
    }
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
      pad_was_down_ = false;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const float atk_hz = 80.f + (1.f - time_norm_) * 220.f;
    const float rel_hz = 1.5f + time_norm_ * 14.f;
    const float atk = fx::onePoleCoeff(atk_hz, getSampleRate());
    const float rel = fx::onePoleCoeff(rel_hz, getSampleRate());
    const float det_hp = fx::onePoleCoeff(40.f + hpf_norm_ * 240.f, getSampleRate());
    const float depth = depth_norm_;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = in[0];
      float live_right = in[1];
      float detect_left = live_left;
      float detect_right = live_right;
      if (raw != nullptr)
        fx::pickLive(in, raw, detect_left, detect_right);

      const float detect_mono = 0.5f * (detect_left + detect_right);
      const float hp = detect_hp_.processHp(detect_mono, det_hp);
      const float env = follower_.process(hp, atk, rel);
      const float ducked = fx::clip01(1.f - env * depth * 3.2f);
      duck_ += (ducked - duck_) * 0.25f;

      float wet_left = live_left;
      float wet_right = live_right;
      if (dest_ == DEST_AMP)
      {
        wet_left *= duck_;
        wet_right *= duck_;
      }
      else if (dest_ == DEST_FILT)
      {
        const float cutoff_hz = 180.f + duck_ * duck_ * 7200.f;
        const float coeff = fx::onePoleCoeff(cutoff_hz, getSampleRate());
        const float filtered = lp_.processLp(0.5f * (live_left + live_right), coeff);
        wet_left = filtered;
        wet_right = filtered;
      }
      else
      {
        const float plate = processPlate(0.5f * (live_left + live_right)) * (0.25f + duck_ * 0.9f);
        wet_left = fx::mix(live_left, plate, 0.85f);
        wet_right = fx::mix(live_right, plate, 0.85f);
      }

      out[0] = fx::mix(live_left, wet_left, mix_);
      out[1] = fx::mix(live_right, wet_right, mix_);
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  float processPlate(float input)
  {
    const uint32_t d1 = 1123U;
    const uint32_t d2 = 1481U;
    float c1 = comb_a_[(write_pos_ + kCombSize - d1) & kCombMask];
    float c2 = comb_b_[(write_pos_ + kCombSize - d2) & kCombMask];
    c1 = input + c1 * 0.72f;
    c2 = input + c2 * 0.67f;
    comb_a_[write_pos_ & kCombMask] = c1;
    comb_b_[write_pos_ & kCombMask] = c2;

    float ap = 0.5f * (c1 + c2);
    const float ap1 = ap_a_[(write_pos_ + kAllpassSize - 241U) & kAllpassMask];
    const float y1 = -0.6f * ap + ap1;
    ap_a_[write_pos_ & kAllpassMask] = ap + 0.6f * y1;
    const float ap2 = ap_b_[(write_pos_ + kAllpassSize - 113U) & kAllpassMask];
    const float y2 = -0.6f * y1 + ap2;
    ap_b_[write_pos_ & kAllpassMask] = y1 + 0.6f * y2;
    ++write_pos_;
    return y2;
  }

  float *comb_a_ = nullptr;
  float *comb_b_ = nullptr;
  float *ap_a_ = nullptr;
  float *ap_b_ = nullptr;
  uint32_t write_pos_ = 0U;
  fx::OnePole detect_hp_;
  fx::OnePole lp_;
  fx::EnvelopeFollower follower_;
  float duck_ = 1.f;
  float time_norm_ = 0.41f;
  float depth_norm_ = 0.76f;
  float hpf_norm_ = 0.18f;
  float mix_ = 1.f;
  uint8_t dest_ = DEST_AMP;
  bool pad_was_down_ = false;
};
