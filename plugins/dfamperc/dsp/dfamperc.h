#pragma once

/*
 * File: dfamperc.h
 *
 * DFAM-inspired mutant percussion for NTS-3.
 * Dual oscillators with sync/FM grit, noise, and a resonant LPF through an
 * age-based snappy envelope (avoid fasterexpf near-1 multiply coeffs).
 * Fixed 8-step pitch + velocity sequence while the pad is held.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class DfamPerc : public Processor
{
public:
  static constexpr uint32_t kSteps = 8U;
  static constexpr uint32_t kTicksPerBar = 16U;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    GRIT = 0U,
    DEC,
    MIX,
    TUNE,
    RES,
    PENV,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case GRIT:
      grit_norm_ = param_10bit_to_f32(value);
      break;
    case DEC:
      dec_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case TUNE:
      tune_norm_ = param_10bit_to_f32(value);
      break;
    case RES:
      res_norm_ = param_10bit_to_f32(value);
      break;
    case PENV:
      penv_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *) override final
  {
    bpm_ = 120.f;
    rng_ = 0xDFA001U;
    // Default 8-step pitch offsets (semitones) and velocities — DFAM-ish kick/tom contour.
    static const float kPitch[8] = {0.f, -5.f, 7.f, 0.f, 12.f, -12.f, 5.f, 0.f};
    static const float kVel[8] = {1.f, 0.55f, 0.85f, 0.4f, 0.95f, 0.7f, 0.5f, 0.8f};
    for (uint32_t stepIndex = 0; stepIndex < kSteps; ++stepIndex)
    {
      pitch_step_[stepIndex] = kPitch[stepIndex];
      vel_step_[stepIndex] = kVel[stepIndex];
    }
    reset();
  }

  void reset() override final
  {
    clock_acc_ = 0.f;
    tick_index_ = 0U;
    step_index_ = 0U;
    age_ = 10.f;
    vel_ = 0.f;
    pitch_env_ = 0.f;
    phase1_ = 0.f;
    phase2_ = 0.f;
    lp_ = fx::OnePole();
    bp_ = fx::OnePole();
    use_host_clock_ = false;
    pad_held_ = false;
    voice_active_ = false;
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
    // 8 steps across a bar => every 2 sixteenth ticks.
    if (((counter - 1U) % 2U) != 0U)
      return;
    triggerStep();
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    const bool held = phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
                      phase == k_unit_touch_phase_stationary;
    if (held && !pad_held_)
    {
      clock_acc_ = 0.f;
      tick_index_ = 0U;
      step_index_ = 0U;
      triggerStep();
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
    const float step_samples = barSamples() / static_cast<float>(kSteps);
    // Age-based body: short stays clicky (~25 ms), long opens into toms (~280 ms).
    const float tau = 0.025f + dec_norm_ * 0.26f;
    const float pitch_tau = 0.012f + (1.f - penv_norm_) * 0.04f;
    const float base_midi = 28.f + tune_norm_ * 36.f;
    const float grit = grit_norm_;
    const float noise_amt = grit * 0.85f;
    const float fm_amt = grit * 1.8f;
    const float res_push = 0.15f + res_norm_ * 0.85f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      if (pad_held_ && !use_host_clock_)
        advanceInternalClock(step_samples);

      float wet = 0.f;
      if (voice_active_ && pad_held_)
      {
        const float amp = fasterexpf(-age_ / tau) * vel_;
        pitch_env_ = fasterexpf(-age_ / pitch_tau);
        const float sweep_semis = penv_norm_ * 18.f * pitch_env_;
        const float midi = base_midi + step_pitch_ + sweep_semis;
        const float inc1 = fx::noteToInc(midi, sample_rate);
        const float inc2 = fx::noteToInc(midi + 0.07f + grit * 7.f, sample_rate);

        phase2_ = fx::wrap01(phase2_ + inc2);
        const float osc2 = fx::blepSaw(phase2_, inc2);
        // Hard sync-ish: reset slave when master wraps, plus FM from osc2.
        const float prev1 = phase1_;
        phase1_ = fx::wrap01(phase1_ + inc1 * (1.f + osc2 * fm_amt * 0.08f));
        if (grit > 0.35f && phase1_ < prev1)
          phase2_ = 0.f;
        const float osc1 = fx::blepPulse(phase1_, inc1, 0.42f + grit * 0.2f);

        const float noise = (fx::randomFloat(rng_) * 2.f - 1.f) * noise_amt;
        const float raw_tone = osc1 * (0.7f - noise_amt * 0.25f) + osc2 * 0.35f + noise;

        const float cutoff = fx::clip(60.f * fastpow2f((0.2f + amp * 0.9f + res_norm_ * 0.4f) * 5.f),
                                      80.f, sample_rate * 0.42f);
        const float lp_coeff = fx::onePoleCoeff(cutoff, sample_rate);
        const float lp = lp_.processLp(raw_tone, lp_coeff);
        // Mild resonance via band-ish feedback without a full SVF.
        const float hp = bp_.processHp(lp, lp_coeff);
        const float resonant = lp + hp * res_push * 0.55f;
        wet = fx::softclip(resonant * amp * 1.55f);

        age_ += 1.f / sample_rate;
        if (amp < 0.0015f || age_ > tau * 8.f)
          voice_active_ = false;
      }

      out[0] = fx::mix(live_left, wet, mix_);
      out[1] = fx::mix(live_right, wet * 0.96f, mix_);
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

  void triggerStep()
  {
    step_pitch_ = pitch_step_[step_index_];
    vel_ = vel_step_[step_index_];
    age_ = 0.f;
    pitch_env_ = 1.f;
    phase1_ = 0.f;
    phase2_ = 0.f;
    voice_active_ = true;
    step_index_ = (step_index_ + 1U) % kSteps;
  }

  void advanceInternalClock(float step_samples)
  {
    if (step_samples <= 1.f)
      return;
    clock_acc_ += 1.f;
    if (clock_acc_ < step_samples)
      return;
    clock_acc_ -= step_samples;
    triggerStep();
    ++tick_index_;
  }

  fx::OnePole lp_;
  fx::OnePole bp_;
  float pitch_step_[8] = {};
  float vel_step_[8] = {};
  float clock_acc_ = 0.f;
  float age_ = 10.f;
  float vel_ = 0.f;
  float pitch_env_ = 0.f;
  float step_pitch_ = 0.f;
  float phase1_ = 0.f;
  float phase2_ = 0.f;
  float bpm_ = 120.f;
  float grit_norm_ = 0.44f;
  float dec_norm_ = 0.37f;
  float tune_norm_ = 0.41f;
  float res_norm_ = 0.51f;
  float penv_norm_ = 0.55f;
  float mix_ = 1.f;
  uint32_t tick_index_ = 0U;
  uint32_t step_index_ = 0U;
  uint32_t rng_ = 1U;
  bool use_host_clock_ = false;
  bool pad_held_ = false;
  bool voice_active_ = false;
};
