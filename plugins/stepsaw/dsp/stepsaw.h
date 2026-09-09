#pragma once

/*
 * File: stepsaw.h
 *
 * Step-grid probe. Passes AUDIO IN and, while the pad is held, overlays a
 * decaying band-limited saw at each tempo step head so you can hear whether
 * the clock/grid is correct. Prefers host 4ppqn when present; otherwise runs
 * an internal sample clock. Touch also resets to step 0 (bar sync).
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class StepSaw : public Processor
{
public:
  static constexpr uint32_t kTicksPerBar = 16U;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    PITCH = 0U,
    DEC,
    MIX,
    STEPS,
    LVL,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case PITCH:
      pitch_norm_ = param_10bit_to_f32(value);
      break;
    case DEC:
      decay_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case STEPS:
      steps_sel_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, 3.f));
      break;
    case LVL:
      level_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    static const char *step_names[4] = {"16", "8", "4", "2"};
    if (index == STEPS && value >= 0 && value <= 3)
      return step_names[value];
    return nullptr;
  }

  void init(float *) override final
  {
    pitch_norm_ = 0.55f;
    decay_norm_ = 0.35f;
    level_norm_ = 0.7f;
    mix_ = 1.f;
    steps_sel_ = 0;
    bpm_ = 120.f;
    reset();
  }

  void reset() override final
  {
    clock_acc_ = 0.f;
    age_ = 0.f;
    phase_ = 0.f;
    tick_counter_ = 0U;
    step_index_ = 0U;
    last_host_counter_ = 0U;
    use_host_clock_ = false;
    pad_held_ = false;
    env_active_ = false;
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 40.f && tempo < 300.f)
      bpm_ = tempo;
  }

  void tempo4ppqnTick(uint32_t counter) override final
  {
    use_host_clock_ = true;
    last_host_counter_ = counter;
    if (!pad_held_)
      return;
    const uint32_t steps = stepsPerBar();
    const uint32_t ticks_per_step = kTicksPerBar / steps;
    if (ticks_per_step == 0U)
      return;
    if (((counter - 1U) % ticks_per_step) != 0U)
      return;
    step_index_ = ((counter - 1U) / ticks_per_step) % steps;
    triggerStep();
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
        phase == k_unit_touch_phase_stationary)
    {
      if (!pad_held_)
      {
        clock_acc_ = 0.f;
        tick_counter_ = 0U;
        step_index_ = 0U;
        triggerStep();
      }
      pad_held_ = true;
      return;
    }
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      pad_held_ = false;
      env_active_ = false;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const float sr = getSampleRate();
    const float midi = 36.f + pitch_norm_ * 48.f;
    const float increment = fx::noteToInc(midi, sr);
    // Age-based decay: avoid fasterexpf for near-1 per-sample coeffs.
    const float tau = 0.008f + decay_norm_ * 0.22f;
    const float marker_gain = mix_ * (0.15f + level_norm_ * 0.85f);
    const float step_samples = barSamples() / static_cast<float>(stepsPerBar());

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      if (pad_held_ && !use_host_clock_)
        advanceInternalClockOneSample(step_samples);

      float marker = 0.f;
      if (pad_held_ && env_active_)
      {
        const float env = fasterexpf(-age_ / tau);
        marker = fx::blepSaw(phase_, increment) * env * marker_gain;
        phase_ = fx::wrap01(phase_ + increment);
        age_ += 1.f / sr;
        if (env < 0.001f || age_ > tau * 8.f)
          env_active_ = false;
      }

      out[0] = live_left + marker;
      out[1] = live_right + marker;
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

  uint32_t debugStepIndex() const { return step_index_; }

  bool debugEnvActive() const { return env_active_; }

  float debugAge() const { return age_; }

  bool debugUsesHostClock() const { return use_host_clock_; }

private:
  uint32_t stepsPerBar() const
  {
    static const uint32_t kSteps[4] = {16U, 8U, 4U, 2U};
    return kSteps[steps_sel_ > 3U ? 0U : steps_sel_];
  }

  float barSamples() const
  {
    return static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()) * 4U);
  }

  void triggerStep()
  {
    age_ = 0.f;
    phase_ = 0.f;
    env_active_ = true;
  }

  void advanceInternalClockOneSample(float step_samples)
  {
    if (step_samples <= 1.f)
      return;
    clock_acc_ += 1.f;
    if (clock_acc_ < step_samples)
      return;
    clock_acc_ -= step_samples;
    const uint32_t steps = stepsPerBar();
    step_index_ = (step_index_ + 1U) % steps;
    ++tick_counter_;
    triggerStep();
  }

  float clock_acc_ = 0.f;
  float age_ = 0.f;
  float phase_ = 0.f;
  float bpm_ = 120.f;
  float pitch_norm_ = 0.55f;
  float decay_norm_ = 0.35f;
  float level_norm_ = 0.7f;
  float mix_ = 1.f;
  uint32_t tick_counter_ = 0U;
  uint32_t step_index_ = 0U;
  uint32_t last_host_counter_ = 0U;
  uint8_t steps_sel_ = 0;
  bool env_active_ = false;
  bool use_host_clock_ = false;
  bool pad_held_ = false;
};
