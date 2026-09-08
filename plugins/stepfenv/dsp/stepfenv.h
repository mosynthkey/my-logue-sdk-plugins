#pragma once

/*
 * File: stepfenv.h
 *
 * Tempo-synced step filter envelope. Every grid step retriggers a cutoff
 * envelope with attack / sustain / release fixed at 0. Decay is the only
 * time parameter. X is the resting cutoff; Y is how far the envelope opens.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class StepFenv : public Processor
{
public:
  static constexpr float kMinFilterCutoffHz = 40.f;
  static constexpr float kMaxFilterCutoffHz = 18000.f;
  static constexpr float kMinDecaySec = 0.015f;
  static constexpr float kMaxDecayOctaves = 6.3f;
  static constexpr float kMaxEnvOctaves = 6.f;
  static constexpr float kParamSmoothCoeff = 0.0025f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    CUT = 0U,
    ENV,
    MIX,
    DEC,
    RES,
    STEPS,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case CUT:
      cutoff_norm_target_ = param_10bit_to_f32(value);
      break;
    case ENV:
      env_depth_target_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case DEC:
      decay_norm_ = param_10bit_to_f32(value);
      break;
    case RES:
      resonance_norm_target_ = param_10bit_to_f32(value);
      break;
    case STEPS:
      steps_sel_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, 2.f));
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index != STEPS)
      return nullptr;
    if (value <= 0)
      return "8";
    if (value == 1)
      return "12";
    return "16";
  }

  void init(float *) override final
  {
    bpm_ = 120.f;
    cutoff_norm_target_ = 0.45f;
    cutoff_norm_smooth_ = 0.45f;
    env_depth_target_ = 0.65f;
    env_depth_smooth_ = 0.65f;
    resonance_norm_target_ = 0.4f;
    resonance_norm_smooth_ = 0.4f;
    decay_norm_ = 0.5f;
    mix_ = 1.f;
    steps_sel_ = 2;
    clock_acc_ = 0.f;
    env_level_ = 0.f;
    step_index_ = 0U;
    svf_left_ = SvfState();
    svf_right_ = SvfState();
    triggerEnvelope();
  }

  void reset() override final
  {
    cutoff_norm_smooth_ = cutoff_norm_target_;
    env_depth_smooth_ = env_depth_target_;
    resonance_norm_smooth_ = resonance_norm_target_;
    clock_acc_ = 0.f;
    env_level_ = 0.f;
    step_index_ = 0U;
    svf_left_ = SvfState();
    svf_right_ = SvfState();
    triggerEnvelope();
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 40.f && tempo < 300.f)
      bpm_ = tempo;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase != k_unit_touch_phase_began)
      return;
    clock_acc_ = 0.f;
    step_index_ = 0U;
    triggerEnvelope();
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const uint32_t steps = (steps_sel_ == 0) ? 8U : (steps_sel_ == 1 ? 12U : 16U);
    const float step_samples =
        static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate())) * 4.f / static_cast<float>(steps);
    const float decay_sec = kMinDecaySec * fasterpow2f(decay_norm_ * kMaxDecayOctaves);
    const float decay_x = -1.f / (decay_sec * getSampleRate());
    const float decay_coeff = 1.f + decay_x;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      cutoff_norm_smooth_ += (cutoff_norm_target_ - cutoff_norm_smooth_) * kParamSmoothCoeff;
      env_depth_smooth_ += (env_depth_target_ - env_depth_smooth_) * kParamSmoothCoeff;
      resonance_norm_smooth_ += (resonance_norm_target_ - resonance_norm_smooth_) * kParamSmoothCoeff;

      clock_acc_ += 1.f;
      if (clock_acc_ >= step_samples)
      {
        clock_acc_ -= step_samples;
        step_index_ = (step_index_ + 1U) % steps;
        triggerEnvelope();
      }

      const float env = env_level_;
      env_level_ *= decay_coeff;
      if (env_level_ < 1.0e-6f)
        env_level_ = 0.f;

      const float cutoff_hz = filterCutoffHz(cutoff_norm_smooth_, env, env_depth_smooth_);
      const float wet_left = processResonantLowpass(live_left, cutoff_hz, resonance_norm_smooth_, svf_left_);
      const float wet_right = processResonantLowpass(live_right, cutoff_hz, resonance_norm_smooth_, svf_right_);

      out[0] = fx::mix(live_left, wet_left, mix_);
      out[1] = fx::mix(live_right, wet_right, mix_);
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  struct SvfState
  {
    float ic1 = 0.f;
    float ic2 = 0.f;
  };

  void triggerEnvelope()
  {
    env_level_ = 1.f;
  }

  static float baseCutoffHz(float cutoff_norm)
  {
    return 70.f * fasterpow2f(cutoff_norm * 8.f);
  }

  static float filterCutoffHz(float cutoff_norm, float env_level, float env_depth)
  {
    const float base_hz = baseCutoffHz(cutoff_norm);
    const float env_mul = fasterpow2f(env_depth * kMaxEnvOctaves * env_level);
    return fx::clip(base_hz * env_mul, kMinFilterCutoffHz, kMaxFilterCutoffHz);
  }

  static float resonanceQ(float resonance_norm)
  {
    return 0.7f + resonance_norm * resonance_norm * 14.f;
  }

  static float resonanceComp(float resonance_norm)
  {
    return 1.f / (1.f + resonance_norm * resonance_norm * 3.5f);
  }

  float processResonantLowpass(float input, float cutoff_hz, float resonance_norm, SvfState &state)
  {
    const float fc = fx::clip(cutoff_hz, kMinFilterCutoffHz, 16000.f);
    const float g = fastertanfullf(3.14159265f * fc / getSampleRate());
    const float k = 1.f / resonanceQ(resonance_norm);
    const float a1 = 1.f / (1.f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;
    const float driven = input * resonanceComp(resonance_norm);

    const float v3 = driven - state.ic2;
    const float v1 = a1 * state.ic1 + a2 * v3;
    const float v2 = state.ic2 + a2 * state.ic1 + a3 * v3;
    state.ic1 = fx::clip(2.f * v1 - state.ic1, -4.f, 4.f);
    state.ic2 = fx::clip(2.f * v2 - state.ic2, -4.f, 4.f);
    return fx::softclip(v2);
  }

  float clock_acc_ = 0.f;
  float env_level_ = 0.f;
  float bpm_ = 120.f;
  float cutoff_norm_target_ = 0.45f;
  float cutoff_norm_smooth_ = 0.45f;
  float env_depth_target_ = 0.65f;
  float env_depth_smooth_ = 0.65f;
  float resonance_norm_target_ = 0.4f;
  float resonance_norm_smooth_ = 0.4f;
  float decay_norm_ = 0.5f;
  float mix_ = 1.f;
  uint32_t step_index_ = 0U;
  uint8_t steps_sel_ = 2;
  SvfState svf_left_;
  SvfState svf_right_;
};
