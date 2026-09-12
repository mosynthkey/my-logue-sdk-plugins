#pragma once

/*
 * File: labipath.h
 *
 * Labyrinth-inspired generative West-Coast voice for NTS-3.
 * Dual 8-step generative sequencers with a Corrupt control (pitch mutation
 * below ~50%, then bit-flip rhythm chaos). Sine carrier + triangle mod osc,
 * through-zero-ish FM, and a soft diode-style wavefolder. Hold pad to run.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class LabiPath : public Processor
{
public:
  static constexpr uint32_t kSteps = 8U;
  static constexpr uint32_t kTicksPerBar = 16U;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    CORR = 0U,
    FOLD,
    MIX,
    ROOT,
    FM,
    DEC,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case CORR:
      corrupt_norm_ = param_10bit_to_f32(value);
      break;
    case FOLD:
      fold_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case ROOT:
      root_note_ = static_cast<float>(fx::clip(static_cast<float>(value), 24.f, 72.f));
      break;
    case FM:
      fm_norm_ = param_10bit_to_f32(value);
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
    bpm_ = 100.f;
    rng_ = 0x1AB17U;
    reset();
    seedSequences(true);
  }

  void reset() override final
  {
    clock_acc_ = 0.f;
    tick_index_ = 0U;
    step_a_ = 0U;
    step_b_ = 0U;
    age_a_ = 10.f;
    age_b_ = 10.f;
    carrier_phase_ = 0.f;
    mod_phase_ = 0.f;
    note_a_ = root_note_;
    note_b_ = root_note_ + 7.f;
    use_host_clock_ = false;
    pad_held_ = false;
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
    // Seq A on every 16th, Seq B on every dotted 8th-ish (3 ticks) for polymeter.
    const uint32_t tick = (counter - 1U) % kTicksPerBar;
    advanceSeqA();
    if ((tick % 3U) == 0U)
      advanceSeqB();
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    const bool held = phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
                      phase == k_unit_touch_phase_stationary;
    if (held && !pad_held_)
    {
      clock_acc_ = 0.f;
      tick_index_ = 0U;
      step_a_ = 0U;
      step_b_ = 0U;
      seedSequences(false);
      triggerA();
      triggerB();
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
    const float tau = 0.03f + dec_norm_ * 0.5f;
    const float fold_drive = 0.6f + fold_norm_ * 3.8f;
    const float fm_depth = fm_norm_ * 2.8f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      if (pad_held_ && !use_host_clock_)
        advanceInternalClock(tick_samples);

      const float env_a = pad_held_ ? fasterexpf(-age_a_ / tau) : 0.f;
      const float env_b = pad_held_ ? fasterexpf(-age_b_ / tau) : 0.f;
      age_a_ += 1.f / sample_rate;
      age_b_ += 1.f / sample_rate;

      const float carrier_inc = fx::noteToInc(note_a_, sample_rate);
      const float mod_inc = fx::noteToInc(note_b_, sample_rate);

      mod_phase_ = fx::wrap01(mod_phase_ + mod_inc);
      // Triangle mod oscillator (West-Coast style modulator).
      const float tri = (mod_phase_ < 0.5f) ? (mod_phase_ * 4.f - 1.f) : (3.f - mod_phase_ * 4.f);
      const float fm = tri * fm_depth * (0.35f + env_b);

      carrier_phase_ = fx::wrap01(carrier_phase_ + carrier_inc * (1.f + fm * 0.15f));
      // Soft sine carrier via phase -> approx sin.
      const float sine = fastersinfullf(carrier_phase_ * 6.283185307179586f + fm);
      const float folded = wavefold(sine * fold_drive) * env_a;
      const float parallel = tri * 0.22f * env_b;
      const float wet = fx::softclip((folded + parallel) * 0.85f);

      out[0] = fx::mix(live_left, wet, mix_);
      out[1] = fx::mix(live_right, wet * 0.94f, mix_);
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

  static float wavefold(float input)
  {
    float x = input;
    // Soft diode-transistor hybrid fold: reflect above ±1 a few times.
    for (uint32_t foldIndex = 0; foldIndex < 3U; ++foldIndex)
    {
      if (x > 1.f)
        x = 2.f - x;
      else if (x < -1.f)
        x = -2.f - x;
      else
        break;
    }
    return fx::softclip(x);
  }

  float quantizeDegree(float unit01) const
  {
    // Minor-pentatonic degrees for musical generative results.
    static const int8_t kScale[5] = {0, 3, 5, 7, 10};
    const float span = unit01 * 14.f; // ~2 octaves of degrees
    const int32_t degree = static_cast<int32_t>(span);
    const int32_t octave = degree / 5;
    const int32_t index = degree - octave * 5;
    const int32_t safe_index = index < 0 ? 0 : (index > 4 ? 4 : index);
    return root_note_ + static_cast<float>(octave * 12 + kScale[safe_index]);
  }

  void seedSequences(bool force_all)
  {
    for (uint32_t stepIndex = 0; stepIndex < kSteps; ++stepIndex)
    {
      if (force_all || gate_a_[stepIndex])
        pitch_a_[stepIndex] = quantizeDegree(fx::randomFloat(rng_));
      if (force_all || gate_b_[stepIndex])
        pitch_b_[stepIndex] = quantizeDegree(fx::randomFloat(rng_));
      if (force_all)
      {
        gate_a_[stepIndex] = fx::randomFloat(rng_) > 0.28f;
        gate_b_[stepIndex] = fx::randomFloat(rng_) > 0.35f;
      }
    }
    length_a_ = 5U + (fx::nextRandom(rng_) % 4U); // 5–8
    length_b_ = 3U + (fx::nextRandom(rng_) % 5U); // 3–7 polymeter
  }

  void maybeCorrupt(uint32_t step, float *pitch, bool *gate)
  {
    const float c = corrupt_norm_;
    if (c <= 0.001f)
      return;
    // Below halfway: mild pitch mutation. Above: also flip gates (Turing-ish).
    if (fx::randomFloat(rng_) < c * 0.45f)
      pitch[step] = quantizeDegree(fx::randomFloat(rng_));
    if (c > 0.5f && fx::randomFloat(rng_) < (c - 0.5f) * 1.4f)
      gate[step] = !gate[step];
  }

  void triggerA()
  {
    if (!gate_a_[step_a_])
      return;
    note_a_ = pitch_a_[step_a_];
    age_a_ = 0.f;
  }

  void triggerB()
  {
    if (!gate_b_[step_b_])
      return;
    note_b_ = pitch_b_[step_b_];
    age_b_ = 0.f;
  }

  void advanceSeqA()
  {
    maybeCorrupt(step_a_, pitch_a_, gate_a_);
    triggerA();
    step_a_ = (step_a_ + 1U) % length_a_;
  }

  void advanceSeqB()
  {
    maybeCorrupt(step_b_, pitch_b_, gate_b_);
    triggerB();
    step_b_ = (step_b_ + 1U) % length_b_;
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
    advanceSeqA();
    if ((tick % 3U) == 0U)
      advanceSeqB();
    ++tick_index_;
  }

  float pitch_a_[8] = {};
  float pitch_b_[8] = {};
  bool gate_a_[8] = {};
  bool gate_b_[8] = {};
  float clock_acc_ = 0.f;
  float age_a_ = 10.f;
  float age_b_ = 10.f;
  float carrier_phase_ = 0.f;
  float mod_phase_ = 0.f;
  float note_a_ = 48.f;
  float note_b_ = 55.f;
  float bpm_ = 100.f;
  float corrupt_norm_ = 0.27f;
  float fold_norm_ = 0.53f;
  float fm_norm_ = 0.35f;
  float dec_norm_ = 0.47f;
  float mix_ = 1.f;
  float root_note_ = 48.f;
  uint32_t tick_index_ = 0U;
  uint32_t step_a_ = 0U;
  uint32_t step_b_ = 0U;
  uint32_t length_a_ = 8U;
  uint32_t length_b_ = 5U;
  uint32_t rng_ = 1U;
  bool use_host_clock_ = false;
  bool pad_held_ = false;
};
