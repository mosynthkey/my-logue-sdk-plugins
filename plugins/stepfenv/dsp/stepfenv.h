#pragma once

/*
 * File: stepfenv.h
 *
 * Tempo-synced step filter envelope. Dry by default; touch engages a resonant
 * low-pass whose cutoff is driven by a retriggered envelope. Period sets the
 * retrigger grid (4 bars down to 1/2 step). Shape selects the envelope curve.
 * X is the resting cutoff; Y is how far the envelope opens.
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
  static constexpr uint8_t kNumPeriods = 8U;
  static constexpr uint8_t kNumShapes = 5U;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    CUT = 0U,
    ENV,
    MIX,
    DEC,
    RES,
    STEPS,
    SHAPE,
    NUM_PARAMS
  };

  enum
  {
    PERIOD_4BAR = 0U,
    PERIOD_2BAR,
    PERIOD_16STEP,
    PERIOD_8STEP,
    PERIOD_4STEP,
    PERIOD_2STEP,
    PERIOD_1STEP,
    PERIOD_HALF
  };

  enum
  {
    SHAPE_SAW = 0U,
    SHAPE_RISE,
    SHAPE_TRI,
    SHAPE_SIGM,
    SHAPE_PULS
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
      period_sel_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, static_cast<float>(kNumPeriods - 1U)));
      break;
    case SHAPE:
      shape_sel_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, static_cast<float>(kNumShapes - 1U)));
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    static const char *period_names[kNumPeriods] = {"4Bar", "2Bar", "16St", "8St", "4St", "2St", "1St", "1/2"};
    static const char *shape_names[kNumShapes] = {"Saw", "Rise", "Tri", "Sigm", "Puls"};
    if (index == STEPS && value >= 0 && value < static_cast<int32_t>(kNumPeriods))
      return period_names[value];
    if (index == SHAPE && value >= 0 && value < static_cast<int32_t>(kNumShapes))
      return shape_names[value];
    return nullptr;
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
    period_sel_ = PERIOD_1STEP;
    shape_sel_ = SHAPE_SAW;
    clock_acc_ = 0.f;
    env_age_ = 0.f;
    pad_held_ = false;
    svf_left_ = SvfState();
    svf_right_ = SvfState();
  }

  void reset() override final
  {
    cutoff_norm_smooth_ = cutoff_norm_target_;
    env_depth_smooth_ = env_depth_target_;
    resonance_norm_smooth_ = resonance_norm_target_;
    clock_acc_ = 0.f;
    env_age_ = 0.f;
    pad_held_ = false;
    svf_left_ = SvfState();
    svf_right_ = SvfState();
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 40.f && tempo < 300.f)
      bpm_ = tempo;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began)
    {
      pad_held_ = true;
      clock_acc_ = 0.f;
      svf_left_ = SvfState();
      svf_right_ = SvfState();
      triggerEnvelope();
      return;
    }
    if (phase == k_unit_touch_phase_moved || phase == k_unit_touch_phase_stationary)
    {
      pad_held_ = true;
      return;
    }
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      pad_held_ = false;
      env_age_ = 1.0e6f;
      svf_left_ = SvfState();
      svf_right_ = SvfState();
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const float sr = getSampleRate();
    const float beat = static_cast<float>(fx::samplesPerBeat(bpm_, sr));
    const float period_samples = beat * 0.25f * periodSixteenths(period_sel_);
    const float decay_sec = kMinDecaySec * fasterpow2f(decay_norm_ * kMaxDecayOctaves);

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      if (!pad_held_)
      {
        out[0] = live_left;
        out[1] = live_right;
        in += 2;
        if (raw != nullptr)
          raw += 2;
        out += 2;
        continue;
      }

      cutoff_norm_smooth_ += (cutoff_norm_target_ - cutoff_norm_smooth_) * kParamSmoothCoeff;
      env_depth_smooth_ += (env_depth_target_ - env_depth_smooth_) * kParamSmoothCoeff;
      resonance_norm_smooth_ += (resonance_norm_target_ - resonance_norm_smooth_) * kParamSmoothCoeff;

      clock_acc_ += 1.f;
      if (clock_acc_ >= period_samples)
      {
        clock_acc_ -= period_samples;
        triggerEnvelope();
      }

      const float env = envelopeLevel(shape_sel_, env_age_, decay_sec);
      env_age_ += 1.f / sr;

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
    env_age_ = 0.f;
  }

  static float periodSixteenths(uint8_t period_sel)
  {
    static const float kPeriods[kNumPeriods] = {64.f, 32.f, 16.f, 8.f, 4.f, 2.f, 1.f, 0.5f};
    return kPeriods[period_sel < kNumPeriods ? period_sel : PERIOD_1STEP];
  }

  static float envelopeLevel(uint8_t shape, float age_sec, float decay_sec)
  {
    if (decay_sec < 1.0e-4f)
      decay_sec = 1.0e-4f;
    const float t = age_sec / decay_sec;

    switch (shape)
    {
    case SHAPE_RISE:
      if (t >= 1.f)
        return 0.f;
      return t;
    case SHAPE_TRI:
      if (t >= 1.f)
        return 0.f;
      if (t < 0.5f)
        return t * 2.f;
      return (1.f - t) * 2.f;
    case SHAPE_SIGM:
    {
      if (t >= 1.2f)
        return 0.f;
      // Smooth high-to-low S-curve across the decay window.
      const float x = (t - 0.5f) * 12.f;
      const float sig = 1.f / (1.f + fasterexpf(x));
      return sig;
    }
    case SHAPE_PULS:
      return (t < 0.5f) ? 1.f : 0.f;
    case SHAPE_SAW:
    default:
      // Age-based exp decay (same family as HClap / StepSaw).
      return fasterexpf(-age_sec / decay_sec);
    }
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
  float env_age_ = 0.f;
  float bpm_ = 120.f;
  float cutoff_norm_target_ = 0.45f;
  float cutoff_norm_smooth_ = 0.45f;
  float env_depth_target_ = 0.65f;
  float env_depth_smooth_ = 0.65f;
  float resonance_norm_target_ = 0.4f;
  float resonance_norm_smooth_ = 0.4f;
  float decay_norm_ = 0.5f;
  float mix_ = 1.f;
  uint8_t period_sel_ = PERIOD_1STEP;
  uint8_t shape_sel_ = SHAPE_SAW;
  bool pad_held_ = false;
  SvfState svf_left_;
  SvfState svf_right_;
};
