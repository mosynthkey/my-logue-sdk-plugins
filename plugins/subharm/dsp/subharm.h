#pragma once

/*
 * File: subharm.h
 *
 * Subharmonicon-inspired polyrhythmic chord voice for NTS-3.
 * Two 4-step sequencers are clocked by layered integer rhythm divisions of
 * the master 16th grid. Two VCOs plus four integer-ratio subs feed a soft
 * one-pole ladder-ish LPF. Prefer an internal sample clock; host 4ppqn is
 * used when present.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class SubHarm : public Processor
{
public:
  static constexpr uint32_t kStepsPerSeq = 4U;
  static constexpr uint32_t kTicksPerBar = 16U;
  static constexpr uint32_t kRhythmCount = 4U;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    POLY = 0U,
    SUBS,
    MIX,
    ROOT,
    CUT,
    DEC,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case POLY:
      poly_norm_ = param_10bit_to_f32(value);
      break;
    case SUBS:
      subs_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case ROOT:
      root_note_ = static_cast<float>(fx::clip(static_cast<float>(value), 24.f, 60.f));
      break;
    case CUT:
      cut_norm_ = param_10bit_to_f32(value);
      break;
    case DEC:
      dec_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *) override final
  {
    bpm_ = 110.f;
    rng_ = 0x51B4A7U;
    reset();
  }

  void reset() override final
  {
    clock_acc_ = 0.f;
    tick_index_ = 0U;
    age_a_ = 10.f;
    age_b_ = 10.f;
    amp_a_ = 0.f;
    amp_b_ = 0.f;
    note_a_ = root_note_;
    note_b_ = root_note_ + 7.f;
    for (uint32_t oscIndex = 0; oscIndex < 6U; ++oscIndex)
      phase_[oscIndex] = fx::randomFloat(rng_);
    lp_ = fx::OnePole();
    use_host_clock_ = false;
    pad_held_ = false;
    static const float kIntervalsA[4] = {0.f, 7.f, 12.f, 5.f};
    static const float kIntervalsB[4] = {0.f, 4.f, 9.f, 16.f};
    for (uint32_t stepIndex = 0; stepIndex < kStepsPerSeq; ++stepIndex)
    {
      // Fixed just-ish intervals so the chord stays coherent without a scale table.
      seq_a_[stepIndex] = kIntervalsA[stepIndex];
      seq_b_[stepIndex] = kIntervalsB[stepIndex];
    }
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 40.f && tempo < 300.f)
      bpm_ = tempo;
  }

  void tempo4ppqnTick(uint32_t counter) override final
  {
    use_host_clock_ = true;
    if (!pad_held_)
      return;
    const uint32_t tick = (counter - 1U) % kTicksPerBar;
    onGridTick(tick);
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    const bool held = phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
                      phase == k_unit_touch_phase_stationary;
    if (held && !pad_held_)
    {
      clock_acc_ = 0.f;
      tick_index_ = 0U;
      onGridTick(0U);
    }
    pad_held_ = held;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const float sample_rate = getSampleRate();
    const float tick_samples = barSamples() / static_cast<float>(kTicksPerBar);
    const float tau = 0.04f + dec_norm_ * 0.42f;
    const float cutoff_hz = 180.f * fastpow2f(cut_norm_ * 5.2f);
    const float lp_coeff = fx::onePoleCoeff(cutoff_hz, sample_rate);
    const float sub_gain = subs_norm_;
    const float main_gain = 0.55f + (1.f - subs_norm_) * 0.35f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      if (pad_held_ && !use_host_clock_)
        advanceInternalClock(tick_samples);

      const float env_a = pad_held_ ? fasterexpf(-age_a_ / tau) : 0.f;
      const float env_b = pad_held_ ? fasterexpf(-age_b_ / tau) : 0.f;
      amp_a_ = env_a;
      amp_b_ = env_b;
      age_a_ += 1.f / sample_rate;
      age_b_ += 1.f / sample_rate;

      // VCO1 + subs /2 /3 ; VCO2 + subs /2 /4
      const float inc0 = fx::noteToInc(note_a_, sample_rate);
      const float inc1 = fx::noteToInc(note_a_ - 12.f, sample_rate); // /2
      const float inc2 = fx::noteToInc(note_a_ - 19.01955f, sample_rate); // ~ /3
      const float inc3 = fx::noteToInc(note_b_, sample_rate);
      const float inc4 = fx::noteToInc(note_b_ - 12.f, sample_rate); // /2
      const float inc5 = fx::noteToInc(note_b_ - 24.f, sample_rate); // /4

      phase_[0] = fx::wrap01(phase_[0] + inc0);
      phase_[1] = fx::wrap01(phase_[1] + inc1);
      phase_[2] = fx::wrap01(phase_[2] + inc2);
      phase_[3] = fx::wrap01(phase_[3] + inc3);
      phase_[4] = fx::wrap01(phase_[4] + inc4);
      phase_[5] = fx::wrap01(phase_[5] + inc5);

      const float vco1 = fx::blepSaw(phase_[0], inc0) * main_gain * amp_a_;
      const float sub1 = fx::blepPulse(phase_[1], inc1, 0.5f) * sub_gain * 0.55f * amp_a_;
      const float sub2 = fx::blepPulse(phase_[2], inc2, 0.5f) * sub_gain * 0.4f * amp_a_;
      const float vco2 = fx::blepSaw(phase_[3], inc3) * main_gain * amp_b_;
      const float sub3 = fx::blepPulse(phase_[4], inc4, 0.5f) * sub_gain * 0.5f * amp_b_;
      const float sub4 = fx::blepPulse(phase_[5], inc5, 0.5f) * sub_gain * 0.35f * amp_b_;

      const float mix_tones = (vco1 + sub1 + sub2 + vco2 + sub3 + sub4) * 0.28f;
      const float filtered = lp_.processLp(mix_tones, lp_coeff);
      const float wet = fx::softclip(filtered * 1.35f);

      out[0] = fx::mix(live_left, wet, mix_);
      out[1] = fx::mix(live_right, wet * 0.97f, mix_);
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  float barSamples() const
  {
    return static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()) * 4U);
  }

  uint32_t activeRhythmMask() const
  {
    // POLY opens more simultaneous rhythm generators.
    const uint32_t count = 1U + static_cast<uint32_t>(poly_norm_ * 3.99f);
    uint32_t mask = 0U;
    for (uint32_t rhythmIndex = 0; rhythmIndex < count && rhythmIndex < kRhythmCount; ++rhythmIndex)
      mask |= (1U << rhythmIndex);
    return mask;
  }

  void onGridTick(uint32_t tick)
  {
    // Rhythm divisions of the 16th grid (Rhythmicon-style integer ratios).
    static const uint32_t kDivisions[4] = {1U, 2U, 3U, 4U};
    const uint32_t mask = activeRhythmMask();
    bool fire_a = false;
    bool fire_b = false;
    for (uint32_t rhythmIndex = 0; rhythmIndex < kRhythmCount; ++rhythmIndex)
    {
      if ((mask & (1U << rhythmIndex)) == 0U)
        continue;
      const uint32_t div = kDivisions[rhythmIndex];
      if ((tick % div) != 0U)
        continue;
      // Odd generators lean on sequencer A, even on B — layered = polyrhythm.
      if ((rhythmIndex & 1U) == 0U)
        fire_a = true;
      else
        fire_b = true;
      if (rhythmIndex >= 2U)
      {
        fire_a = true;
        fire_b = true;
      }
    }

    if (fire_a)
    {
      const uint32_t step = (tick / kDivisions[0]) % kStepsPerSeq;
      note_a_ = root_note_ + seq_a_[step];
      age_a_ = 0.f;
    }
    if (fire_b)
    {
      const uint32_t step = (tick / kDivisions[1]) % kStepsPerSeq;
      note_b_ = root_note_ + seq_b_[step];
      age_b_ = 0.f;
    }
  }

  void advanceInternalClock(float tick_samples)
  {
    if (tick_samples <= 1.f)
      return;
    clock_acc_ += 1.f;
    if (clock_acc_ < tick_samples)
      return;
    clock_acc_ -= tick_samples;
    const uint32_t tick = tick_index_ % kTicksPerBar;
    onGridTick(tick);
    ++tick_index_;
  }

  fx::OnePole lp_;
  float phase_[6] = {};
  float seq_a_[4] = {};
  float seq_b_[4] = {};
  float clock_acc_ = 0.f;
  float age_a_ = 10.f;
  float age_b_ = 10.f;
  float amp_a_ = 0.f;
  float amp_b_ = 0.f;
  float note_a_ = 36.f;
  float note_b_ = 43.f;
  float bpm_ = 110.f;
  float poly_norm_ = 0.4f;
  float subs_norm_ = 0.6f;
  float cut_norm_ = 0.66f;
  float dec_norm_ = 0.5f;
  float mix_ = 1.f;
  float root_note_ = 36.f;
  uint32_t tick_index_ = 0U;
  uint32_t rng_ = 1U;
  bool use_host_clock_ = false;
  bool pad_held_ = false;
};
