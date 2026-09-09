#pragma once

/*
 * Shared pad-phrase helpers for HSnare / HClap (and similar units).
 * Tempo 16-step Euclidean density with backbeat rotation, flam on dense
 * backbeats, and 808-analog ↔ 909-LFSR noise morph. Header-only, libm-free.
 */

#include "fx_dsp.h"
#include "processor.h"
#include "runtime.h"
#include "tr909_pcm.h"
#include <stdint.h>

namespace hpad
{

constexpr uint32_t kStepsPerBar = 16U;
constexpr float kAnalogColorHz = 9000.f;
constexpr float kFlamSixteenthFraction = 0.5f;
constexpr float kFlamDensityGate = 0.82f;
constexpr float kFlamAccent = 0.72f;

inline uint32_t hitsFromDensity(float dens_norm)
{
  uint32_t hits = 1U + static_cast<uint32_t>(dens_norm * 15.f + 0.5f);
  if (hits < 1U)
    hits = 1U;
  if (hits > 16U)
    hits = 16U;
  return hits;
}

// Rotate Euclid so sparse hits land on beats 2 and 4 (steps 4 and 12).
inline bool stepIsHit(uint32_t step_index, uint32_t hits)
{
  return fx::euclidHit((step_index + 12U) % kStepsPerBar, hits, kStepsPerBar);
}

inline bool isBackbeat(uint32_t step_index)
{
  return step_index == 4U || step_index == 12U;
}

inline float samplesPerSixteenth(float bpm, float sample_rate)
{
  if (bpm <= 0.f || sample_rate <= 0.f)
    return 0.f;
  return sample_rate * 60.f / (bpm * 4.f);
}

// Per-sample multiply near 1. Prefer over fasterexpf for tiny |x|.
inline float envCoeffNearOne(float seconds, float sample_rate)
{
  const float clamped = fx::clip(seconds, 0.008f, 0.8f);
  const float rate = (sample_rate > 1.f) ? sample_rate : 48000.f;
  const float x = -1.f / (clamped * rate);
  return fx::clip(1.f + x, 0.f, 1.f);
}

struct MorphNoise
{
  uint32_t analog_state = 1U;
  uint32_t lfsr = 0x7FFFFFFFu;
  float lp = 0.f;
  float color_coeff = 0.7f;

  void init(uint32_t analog_seed, float sample_rate)
  {
    analog_state = analog_seed;
    lfsr = 0x7FFFFFFFu;
    lp = 0.f;
    color_coeff = fx::onePoleCoeff(kAnalogColorHz, sample_rate);
  }

  void resetFilters() { lp = 0.f; }

  float analogSample()
  {
    analog_state = analog_state * 1664525U + 1013904223U;
    return (static_cast<float>(analog_state) * (1.f / 2147483648.f)) - 1.f;
  }

  float lfsrSample()
  {
    const uint32_t bit = ((lfsr >> 30) ^ (lfsr >> 12)) & 1U;
    lfsr = ((lfsr << 1) | bit) & 0x7FFFFFFFu;
    if (lfsr == 0U)
      lfsr = 0x7FFFFFFFu;
    return (lfsr & 1U) ? 1.f : -1.f;
  }

  float next(float type_norm)
  {
    lp += color_coeff * (analogSample() - lp);
    const float digital = lfsrSample();
    return lp + type_norm * (digital - lp);
  }
};

// CRTP: derived implements onPhraseHit(accent).
template <typename Derived>
class PhraseProcessor : public Processor
{
public:
  void setTempo(float tempo) override final
  {
    if (tempo > 20.f && tempo < 999.f)
      bpm_ = tempo;
  }

  void tempo4ppqnTick(uint32_t counter) override final
  {
    use_host_clock_ = true;
    handleTick(counter);
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
        phase == k_unit_touch_phase_stationary)
    {
      running_ = true;
      return;
    }

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      running_ = false;
      flam_samples_left_ = 0;
    }
  }

  uint32_t debugHits() const { return hitsFromDensity(dens_norm_); }

  bool debugStepHit(uint32_t step_index) const
  {
    return stepIsHit(step_index, hitsFromDensity(dens_norm_));
  }

protected:
  Derived *self() { return static_cast<Derived *>(this); }
  const Derived *self() const { return static_cast<const Derived *>(this); }

  void initPhrase(uint32_t analog_seed)
  {
    dens_norm_ = 0.068f;
    type_norm_ = 0.f;
    mix_ = 1.f;
    bpm_ = 120.f;
    running_ = false;
    use_host_clock_ = false;
    tick_counter_ = 0U;
    internal_tick_phase_ = 0.f;
    flam_samples_left_ = 0;
    dc_prev_in_ = 0.f;
    dc_prev_out_ = 0.f;
    noise_.init(analog_seed, getSampleRate());
  }

  void resetPhrase()
  {
    running_ = false;
    flam_samples_left_ = 0;
    dc_prev_in_ = 0.f;
    dc_prev_out_ = 0.f;
    noise_.resetFilters();
  }

  void handleTick(uint32_t counter)
  {
    tick_counter_ = counter;
    if (!running_)
      return;

    const uint32_t step_index = (counter - 1U) % kStepsPerBar;
    const uint32_t hits = hitsFromDensity(dens_norm_);
    if (!stepIsHit(step_index, hits))
      return;

    self()->onPhraseHit(1.f);
    if (dens_norm_ > kFlamDensityGate && isBackbeat(step_index) && bpm_ > 0.f)
    {
      const float samples_per_16th = samplesPerSixteenth(bpm_, getSampleRate());
      flam_samples_left_ = static_cast<int32_t>(samples_per_16th * kFlamSixteenthFraction);
    }
  }

  void advanceInternalClockOneSample()
  {
    if (bpm_ <= 0.f)
      return;

    const float samples_per_tick = samplesPerSixteenth(bpm_, getSampleRate());
    if (samples_per_tick <= 0.f)
      return;

    internal_tick_phase_ += 1.f;
    if (internal_tick_phase_ >= samples_per_tick)
    {
      internal_tick_phase_ -= samples_per_tick;
      ++tick_counter_;
      handleTick(tick_counter_);
    }
  }

  void advancePendingFlam()
  {
    if (flam_samples_left_ <= 0)
      return;
    --flam_samples_left_;
    if (flam_samples_left_ == 0)
      self()->onPhraseHit(kFlamAccent);
  }

  float dcBlockSum(float sum)
  {
    return tr909::dcBlock(sum, dc_prev_in_, dc_prev_out_);
  }

  MorphNoise noise_;
  uint32_t tick_counter_ = 0U;
  int32_t flam_samples_left_ = 0;
  float dens_norm_ = 0.f;
  float type_norm_ = 0.f;
  float mix_ = 1.f;
  float bpm_ = 120.f;
  float internal_tick_phase_ = 0.f;
  float dc_prev_in_ = 0.f;
  float dc_prev_out_ = 0.f;
  bool running_ = false;
  bool use_host_clock_ = false;
};

} // namespace hpad
