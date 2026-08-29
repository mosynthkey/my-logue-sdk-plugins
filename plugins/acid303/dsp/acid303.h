#pragma once

/*
 * File: acid303.h
 *
 * TB-303 style monophonic acid bass with automatic 16-step phrase generator
 * for NTS-3 kaoss pad. Hold the pad to run the sequencer; each new touch
 * regenerates a random acid pattern. X = cutoff, Y = resonance, Depth = mix.
 *
 * Filter/voice structure follows the gsynth TB-303 module (Andy Sloane, 2001),
 * adapted for 48 kHz and portamento/slide/accent behaviour.
 */

#include "macros.h"
#include "processor.h"
#include "unit_genericfx.h"
#include <math.h>
#include <stdint.h>

class Acid303 : public Processor
{
public:
  static constexpr uint32_t kStepsPerBar = 16U;
  static constexpr uint32_t kFilterEnvRecalcInterval = 64U;
  static constexpr float kOutputGain = 0.55f;
  static constexpr float kSlideRate = 0.0025f;
  static constexpr float kAccentBoost = 1.35f;
  static constexpr float kAccentCutoffBoost = 0.18f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    CUT = 0U,
    RES,
    MIX,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case CUT:
      cutoff_norm_ = param_10bit_to_f32(value);
      updateFilterTargets();
      break;
    case RES:
      resonance_norm_ = param_10bit_to_f32(value);
      updateFilterTargets();
      break;
    case MIX:
      mix_ = value / 1000.f;
      if (mix_ < 0.f)
        mix_ = 0.f;
      if (mix_ > 1.f)
        mix_ = 1.f;
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    (void)index;
    (void)value;
    return nullptr;
  }

  void init(float *) override final
  {
    cutoff_norm_ = 0.62f;
    resonance_norm_ = 0.55f;
    mix_ = 1.f;
    bpm_ = 120.f;
    running_ = false;
    use_host_clock_ = false;
    tick_counter_ = 0U;
    internal_tick_phase_ = 0.f;
    phrase_seed_ = 1U;
    resetVoice();
    updateFilterTargets();
    generatePhrase(phrase_seed_);
  }

  void reset() override final
  {
    running_ = false;
    resetVoice();
  }

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

  void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    (void)id;

    if (phase == k_unit_touch_phase_began)
    {
      phrase_seed_ = mixSeed(tick_counter_, x, y);
      generatePhrase(phrase_seed_);
      running_ = true;
      triggerStep(phrase_[0]);
      return;
    }

    if (phase == k_unit_touch_phase_moved || phase == k_unit_touch_phase_stationary)
      return;

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      running_ = false;
      gate_off_requested_ = true;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    if (!use_host_clock_)
      advanceInternalClock(frames);

    const float dry_gain = 1.f - mix_;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      const float wet = renderSample() * mix_ * kOutputGain;
      out[0] = in[0] * dry_gain + wet;
      out[1] = in[1] * dry_gain + wet;
      in += 2;
      out += 2;
    }
  }

private:
  struct Step
  {
    int8_t note = -1;
    bool accent = false;
    bool slide = false;
  };

  static uint32_t stepOneBased(uint32_t counter)
  {
    return ((counter - 1U) % kStepsPerBar) + 1U;
  }

  static uint32_t mixSeed(uint32_t counter, uint32_t x, uint32_t y)
  {
    return counter * 2654435761U + x * 2246822519U + y * 3266489917U + 1U;
  }

  static uint32_t nextRandom(uint32_t &state)
  {
    state = state * 1664525U + 1013904223U;
    return state;
  }

  static float randomFloat(uint32_t &state)
  {
    return static_cast<float>(nextRandom(state) >> 8) * (1.f / 16777216.f);
  }

  static bool euclideanGate(uint32_t stepIndex, uint32_t pulses, uint32_t steps)
  {
    return ((stepIndex * pulses) % steps) < pulses;
  }

  static float noteToPhaseInc(int8_t note)
  {
    const float semitones = static_cast<float>(note - 57);
    return (440.f / getSampleRate()) * exp2f(semitones * (1.f / 12.f));
  }

  void resetVoice()
  {
    vco_phase_inc_ = 0.f;
    vco_phase_inc_target_ = 0.f;
    vco_phase_ = 0.f;
    vcf_cutoff_ = 0.f;
    vcf_env_mod_ = 0.58f;
    vcf_reso_ = 0.f;
    vcf_res_coeff_ = 0.f;
    vcf_env_decay_ = 0.f;
    vcf_env_pos_ = kFilterEnvRecalcInterval;
    vcf_a_ = 0.f;
    vcf_b_ = 0.f;
    vcf_c_ = 0.f;
    vcf_delay1_ = 0.f;
    vcf_delay2_ = 0.f;
    vcf_env_level_ = 0.f;
    vcf_env_end0_ = 0.f;
    vcf_env_span_ = 0.f;
    vca_mode_ = 2;
    vca_level_ = 0.f;
    vca_attack_ = 1.f - 0.94406088f;
    vca_decay_ = 0.99897516f;
    vca_target_ = 0.5f;
    gate_off_requested_ = false;
    slide_active_ = false;
    accent_active_ = false;
    dc_prev_in_ = 0.f;
    dc_prev_out_ = 0.f;
  }

  void updateFilterTargets()
  {
    vcf_cutoff_ = 0.12f + cutoff_norm_ * 0.78f;
    if (vcf_cutoff_ > 1.f)
      vcf_cutoff_ = 1.f;
    vcf_reso_ = 0.05f + resonance_norm_ * 0.9f;
    if (vcf_reso_ > 1.f)
      vcf_reso_ = 1.f;
    vcf_env_mod_ = 0.42f + resonance_norm_ * 0.4f;

    vcf_res_coeff_ = expf(-1.20f + 3.455f * vcf_reso_);
    recalcFilterEnvelope();
  }

  void recalcFilterEnvelope()
  {
    const float res_comp = 1.f - vcf_reso_;
    const float env_end1 = expf(6.109f + 1.5876f * vcf_env_mod_ + 2.1553f * vcf_cutoff_ - 1.2f * res_comp);
    const float env_end0 = expf(5.613f - 0.8f * vcf_env_mod_ + 2.1553f * vcf_cutoff_ - 0.7696f * res_comp);
    const float sample_rate_scale = 3.141592653589793f / getSampleRate();

    vcf_env_end0_ = env_end0 * sample_rate_scale;
    vcf_env_span_ = (env_end1 - env_end0) * sample_rate_scale;

    const float decay_seconds = 0.2f + 2.3f * (0.35f + cutoff_norm_ * 0.45f);
    const float decay_samples = decay_seconds * getSampleRate();
    vcf_env_decay_ = powf(0.1f, static_cast<float>(kFilterEnvRecalcInterval) / decay_samples);
    vcf_env_pos_ = kFilterEnvRecalcInterval;
  }

  void generatePhrase(uint32_t seed)
  {
    uint32_t rng = seed;

    const int8_t root_note = static_cast<int8_t>(36 + static_cast<int32_t>(randomFloat(rng) * 8.f));
    static const int8_t kScaleDegrees[] = {0, 3, 5, 7, 10, 12, 15, 17};
    static constexpr uint32_t kScaleLength = sizeof(kScaleDegrees) / sizeof(kScaleDegrees[0]);

    const uint32_t pulse_count = 4U + (nextRandom(rng) % 5U);
    int8_t previous_note = -1;

    for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
    {
      Step &step = phrase_[stepIndex];
      step.accent = false;
      step.slide = false;
      step.note = -1;

      if (!euclideanGate(stepIndex, pulse_count, kStepsPerBar))
        continue;

      const uint32_t degree_index = nextRandom(rng) % kScaleLength;
      step.note = static_cast<int8_t>(root_note + kScaleDegrees[degree_index]);

      if ((stepIndex % 4U) == 0U || randomFloat(rng) > 0.62f)
        step.accent = true;

      if (previous_note >= 0 && step.note == previous_note)
        step.slide = true;
      else if (randomFloat(rng) > 0.58f && previous_note >= 0)
        step.slide = true;

      previous_note = step.note;
    }

    if (phrase_[0].note < 0)
    {
      phrase_[0].note = root_note;
      phrase_[0].accent = true;
    }
  }

  void triggerStep(const Step &step)
  {
    if (step.note < 0)
    {
      gate_off_requested_ = true;
      return;
    }

    accent_active_ = step.accent;
    vca_target_ = step.accent ? (0.5f * kAccentBoost) : 0.5f;

    const float phase_inc = noteToPhaseInc(step.note);
    vco_phase_inc_target_ = phase_inc;

    if (step.slide && vco_phase_inc_ > 0.f)
    {
      slide_active_ = true;
    }
    else
    {
      slide_active_ = false;
      vco_phase_inc_ = phase_inc;
    }

    vca_mode_ = 0;
    vcf_env_level_ = vcf_env_span_;
    if (accent_active_)
      vcf_env_level_ += vcf_env_span_ * kAccentCutoffBoost;
    vcf_env_pos_ = kFilterEnvRecalcInterval;
    gate_off_requested_ = false;
  }

  void handleTick(uint32_t counter)
  {
    tick_counter_ = counter;
    if (!running_)
      return;

    const uint32_t step_index = stepOneBased(counter) - 1U;
    triggerStep(phrase_[step_index]);
  }

  void advanceInternalClock(uint32_t frames)
  {
    if (!running_ || bpm_ <= 0.f)
      return;

    const float samples_per_tick = getSampleRate() * 60.f / (bpm_ * 4.f);
    internal_tick_phase_ += static_cast<float>(frames);

    while (internal_tick_phase_ >= samples_per_tick)
    {
      internal_tick_phase_ -= samples_per_tick;
      ++tick_counter_;
      handleTick(tick_counter_);
    }
  }

  void updateFilterCoefficients()
  {
    const float w = vcf_env_end0_ + vcf_env_level_;
    const float k = expf(-w / vcf_res_coeff_);
    vcf_env_level_ *= vcf_env_decay_;
    vcf_a_ = 2.f * cosf(2.f * w) * k;
    vcf_b_ = -k * k;
    vcf_c_ = 1.f - vcf_a_ - vcf_b_;
    vcf_env_pos_ = 0U;
  }

  void advanceSlide()
  {
    if (!slide_active_)
      return;

    const float delta = vco_phase_inc_target_ - vco_phase_inc_;
    if (fabsf(delta) <= kSlideRate)
    {
      vco_phase_inc_ = vco_phase_inc_target_;
      slide_active_ = false;
      return;
    }

    vco_phase_inc_ += (delta > 0.f) ? kSlideRate : -kSlideRate;
  }

  float renderSample()
  {
    if (gate_off_requested_ && vca_mode_ == 2 && vca_level_ <= 0.f)
      return 0.f;

    if (vcf_env_pos_ >= kFilterEnvRecalcInterval)
      updateFilterCoefficients();

    advanceSlide();

    const float oscillator = vco_phase_;
    const float filtered = vcf_a_ * vcf_delay1_ + vcf_b_ * vcf_delay2_ + vcf_c_ * oscillator * vca_level_;
    vcf_delay2_ = vcf_delay1_;
    vcf_delay1_ = filtered;
    ++vcf_env_pos_;

    vco_phase_ += vco_phase_inc_;
    if (vco_phase_ > 0.5f)
      vco_phase_ -= 1.f;

    if (vca_mode_ == 0)
      vca_level_ += (vca_target_ - vca_level_) * vca_attack_;
    else if (vca_mode_ == 1)
    {
      vca_level_ *= vca_decay_;
      if (vca_level_ < (1.f / 65536.f))
      {
        vca_level_ = 0.f;
        vca_mode_ = 2;
      }
    }

    if (gate_off_requested_)
    {
      vca_mode_ = 1;
      gate_off_requested_ = false;
    }

    const float blocked = filtered - dc_prev_in_ + 0.99608f * dc_prev_out_;
    dc_prev_in_ = filtered;
    dc_prev_out_ = blocked;
    return blocked;
  }

  Step phrase_[kStepsPerBar];
  uint32_t phrase_seed_ = 1U;
  uint32_t tick_counter_ = 0U;
  float cutoff_norm_ = 0.62f;
  float resonance_norm_ = 0.55f;
  float mix_ = 1.f;
  float bpm_ = 120.f;
  float internal_tick_phase_ = 0.f;
  float vco_phase_inc_ = 0.f;
  float vco_phase_inc_target_ = 0.f;
  float vco_phase_ = 0.f;
  float vcf_cutoff_ = 0.f;
  float vcf_env_mod_ = 0.f;
  float vcf_reso_ = 0.f;
  float vcf_res_coeff_ = 0.f;
  float vcf_env_decay_ = 0.f;
  float vcf_a_ = 0.f;
  float vcf_b_ = 0.f;
  float vcf_c_ = 0.f;
  float vcf_delay1_ = 0.f;
  float vcf_delay2_ = 0.f;
  float vcf_env_level_ = 0.f;
  float vcf_env_end0_ = 0.f;
  float vcf_env_span_ = 0.f;
  float vca_level_ = 0.f;
  float vca_attack_ = 0.f;
  float vca_decay_ = 0.f;
  float vca_target_ = 0.5f;
  float dc_prev_in_ = 0.f;
  float dc_prev_out_ = 0.f;
  uint32_t vcf_env_pos_ = kFilterEnvRecalcInterval;
  int vca_mode_ = 2;
  bool running_ = false;
  bool use_host_clock_ = false;
  bool gate_off_requested_ = false;
  bool slide_active_ = false;
  bool accent_active_ = false;
};
