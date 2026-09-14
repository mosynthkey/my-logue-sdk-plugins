#pragma once

/*
 * File: s4ring.h
 *
 * Torso S-4 RING inspired morphing resonant 48-band filterbank.
 * Parallel Chamberlin bandpasses with an LP→BP→HP spectral envelope
 * (Slope), per-band Q from Decay, scale-quantized centers, and Tone tilt.
 * Pad-held wet (NTS-3 performance pattern).
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class S4Ring : public Processor
{
public:
  static constexpr uint32_t kBandCount = 48U;
  static constexpr float kPi = 3.141592653589793f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    CUT = 0U,
    DEC,
    MIX,
    RES,
    SLOP,
    PTCH,
    SCAL,
    TONE,
    NUM_PARAMS
  };

  enum
  {
    SCALE_CHR = 0,
    SCALE_MAJ,
    SCALE_MIN,
    SCALE_PEN,
    SCALE_BLU,
    SCALE_WHL,
    SCALE_COUNT
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case CUT:
      cutoff_norm_ = param_10bit_to_f32(value);
      break;
    case DEC:
      decay_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case RES:
      reso_norm_ = param_10bit_to_f32(value);
      break;
    case SLOP:
      slope_norm_ = param_10bit_to_f32(value);
      break;
    case PTCH:
      pitch_semi_ = fx::clip(static_cast<float>(value), -24.f, 24.f);
      break;
    case SCAL:
      scale_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, SCALE_COUNT - 1.f));
      break;
    case TONE:
      tone_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index != SCAL)
      return nullptr;
    static const char *kNames[SCALE_COUNT] = {"CHR", "MAJ", "MIN", "PEN", "BLU", "WHL"};
    if (value < 0)
      value = 0;
    if (value >= SCALE_COUNT)
      value = SCALE_COUNT - 1;
    return kNames[value];
  }

  void init(float *) override final { resetState(); }

  void teardown() override final {}

  void reset() override final { resetState(); }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
        phase == k_unit_touch_phase_stationary)
    {
      pad_held_ = true;
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
    updateBandTable();

    // Decay raises per-band Q (ringing). Keep Chamberlin damp in a stable range.
    const float decay_shaped = decay_norm_ * decay_norm_;
    const float q = 0.9f + decay_shaped * 220.f + reso_norm_ * 24.f;
    const float damp = fx::clip(1.f / q, 0.003f, 1.2f);
    const float makeup = 1.f / (0.5f + 0.045f * q);
    const float wet_coeff = 1.f - fasterexpf(-1.f / 128.f);
    const float drive = 1.25f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);
      const float excite = 0.5f * (live_left + live_right);

      wet_env_ += ((pad_held_ ? 1.f : 0.f) - wet_env_) * wet_coeff;

      float wet_left = 0.f;
      float wet_right = 0.f;
      for (uint32_t bandIndex = 0; bandIndex < kBandCount; ++bandIndex)
      {
        const float f = band_f_[bandIndex];
        const float gain = band_gain_[bandIndex];
        float low = band_low_[bandIndex];
        float bp = band_bp_[bandIndex];

        low += f * bp;
        const float hp = excite - low - damp * bp;
        bp += f * hp;
        low = fx::clip(low, -8.f, 8.f);
        bp = fx::clip(bp, -8.f, 8.f);
        band_low_[bandIndex] = low;
        band_bp_[bandIndex] = bp;

        const float band_out = bp * gain * makeup;
        const float pan = band_pan_[bandIndex];
        wet_left += band_out * (0.5f - 0.5f * pan);
        wet_right += band_out * (0.5f + 0.5f * pan);
      }

      wet_left = fx::softclip(wet_left * drive);
      wet_right = fx::softclip(wet_right * drive);

      const float amount = wet_env_ * mix_;
      out[0] = fx::mix(live_left, wet_left, amount);
      out[1] = fx::mix(live_right, wet_right, amount);
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  void resetState()
  {
    for (uint32_t bandIndex = 0; bandIndex < kBandCount; ++bandIndex)
    {
      band_low_[bandIndex] = 0.f;
      band_bp_[bandIndex] = 0.f;
      band_f_[bandIndex] = 0.f;
      band_gain_[bandIndex] = 0.f;
      band_pan_[bandIndex] = 0.f;
    }
    wet_env_ = 0.f;
    pad_held_ = false;
  }

  static float quantizeToScale(float midi_note, uint8_t scale)
  {
    if (scale == SCALE_CHR)
      return midi_note;

    static const uint8_t kMask[SCALE_COUNT][12] = {
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, // CHR
        {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1}, // MAJ
        {1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0}, // MIN
        {1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0}, // PEN major
        {1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 1, 0}, // BLU
        {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0}, // WHL
    };

    const int rounded = static_cast<int>(midi_note + (midi_note >= 0.f ? 0.5f : -0.5f));
    int best = rounded;
    int best_dist = 128;
    for (int delta = -6; delta <= 6; ++delta)
    {
      const int candidate = rounded + delta;
      int pitch_class = candidate % 12;
      if (pitch_class < 0)
        pitch_class += 12;
      if (kMask[scale][pitch_class] == 0)
        continue;
      const int dist = delta < 0 ? -delta : delta;
      if (dist < best_dist)
      {
        best_dist = dist;
        best = candidate;
      }
    }
    return static_cast<float>(best);
  }

  static float morphWeight(float octaves, float slope)
  {
    const float steep = 2.8f;
    const float lp = 1.f / (1.f + fasterexpf(steep * octaves));
    const float hp = 1.f / (1.f + fasterexpf(-steep * octaves));
    // Use 1+x near 0 so BP center is not biased by fasterexpf(0)≈0.971.
    const float bp_x = -octaves * octaves * 2.2f;
    const float bp = (bp_x > -0.2f) ? (1.f + bp_x) : fasterexpf(bp_x);

    if (slope <= 0.5f)
      return fx::mix(lp, bp, slope * 2.f);
    return fx::mix(bp, hp, (slope - 0.5f) * 2.f);
  }

  void updateBandTable()
  {
    const float sample_rate = getSampleRate();
    const float base_note = 36.f + pitch_semi_;
    const float cutoff_note = base_note + cutoff_norm_ * 47.f;
    const float tone = tone_norm_ * 2.f - 1.f;
    const float reso = reso_norm_;

    float gain_sum = 0.f;
    for (uint32_t bandIndex = 0; bandIndex < kBandCount; ++bandIndex)
    {
      const float raw_note = base_note + static_cast<float>(bandIndex);
      const float note = quantizeToScale(raw_note, scale_);
      const float hz = fx::clip(fx::noteToHz(note), 40.f, sample_rate * 0.42f);
      // Chamberlin f = 2*sin(pi*fc/fs); small-angle ≈ 2*pi*fc/fs.
      const float f = fx::clip(2.f * kPi * hz / sample_rate, 0.001f, 0.95f);
      band_f_[bandIndex] = f;

      const float octaves = (note - cutoff_note) * (1.f / 12.f);
      float weight = morphWeight(octaves, slope_norm_);
      const float near_x = -octaves * octaves * 6.f;
      const float near = (near_x > -0.2f) ? (1.f + near_x) : fasterexpf(near_x);
      weight += reso * near * 1.25f;
      const float tilt = 1.f + tone * (static_cast<float>(bandIndex) * (2.f / 47.f) - 1.f);
      weight *= fx::clip(tilt, 0.15f, 2.2f);
      if (weight < 0.f)
        weight = 0.f;

      band_gain_[bandIndex] = weight;
      band_pan_[bandIndex] = static_cast<float>(bandIndex) * (2.f / 47.f) - 1.f;
      gain_sum += weight;
    }

    const float norm = (gain_sum > 0.001f) ? (1.6f / gain_sum) : 0.f;
    for (uint32_t bandIndex = 0; bandIndex < kBandCount; ++bandIndex)
      band_gain_[bandIndex] *= norm;
  }

  float band_low_[kBandCount] = {};
  float band_bp_[kBandCount] = {};
  float band_f_[kBandCount] = {};
  float band_gain_[kBandCount] = {};
  float band_pan_[kBandCount] = {};

  float cutoff_norm_ = 0.45f;
  float decay_norm_ = 0.35f;
  float reso_norm_ = 0.25f;
  float slope_norm_ = 0.5f;
  float pitch_semi_ = 0.f;
  float tone_norm_ = 0.5f;
  float mix_ = 0.85f;
  float wet_env_ = 0.f;
  uint8_t scale_ = SCALE_CHR;
  bool pad_held_ = false;
};
