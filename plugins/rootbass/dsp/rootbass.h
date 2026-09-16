#pragma once

/*
 * File: rootbass.h
 *
 * Lowest-pitch follower bass for NTS-3.
 * Detects the input's bass-range fundamental (AMDF, 40–220 Hz), quantizes to
 * 12-TET A440, and gates a synth oscillator on a fixed rhythm. Hold to run;
 * hits lock to tempo.
 *
 * Pitch model: the chord is assumed stable *inside* one 16th-note step, so the
 * voice latches the detector only on step boundaries (and can follow chord
 * changes from step to step). HOLD mode follows the detector continuously.
 *
 * Featured rhythm: Tresillo (3+3+2), the clave cell behind dembow / reggaeton.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class RootBass : public Processor
{
public:
  static constexpr uint32_t kSteps = 16U;
  static constexpr uint32_t kDownsample = 8U;
  static constexpr uint32_t kDetectSize = 256U;
  static constexpr uint32_t kDetectMask = kDetectSize - 1U;
  static constexpr float kTwoPi = 6.283185307179586f;
  static constexpr float kDetectRate = 48000.f / static_cast<float>(kDownsample);

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    OCT = 0U,
    WAVE,
    MIX,
    RHY,
    DEC,
    NUM_PARAMS
  };

  enum
  {
    RHY_HOLD = 0,
    RHY_TRESI = 1,
    RHY_CINQ = 2,
    RHY_HALF = 3
  };

  enum
  {
    WAVE_SIN = 0,
    WAVE_TRI = 1,
    WAVE_SAW = 2,
    WAVE_SQR = 3
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case OCT:
      octave_ = static_cast<int8_t>(fx::clip(static_cast<float>(value), 0.f, 3.f)) - 2;
      break;
    case WAVE:
      wave_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, 3.f));
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case RHY:
      rhythm_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, 3.f));
      break;
    case DEC:
      decay_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index == WAVE)
    {
      if (value <= 0)
        return "SIN";
      if (value == 1)
        return "TRI";
      if (value == 2)
        return "SAW";
      return "SQR";
    }
    if (index == RHY)
    {
      if (value <= 0)
        return "HOLD";
      if (value == 1)
        return "TRESI";
      if (value == 2)
        return "CINQ";
      return "HALF";
    }
    if (index == OCT)
    {
      if (value <= 0)
        return "-2";
      if (value == 1)
        return "-1";
      if (value == 2)
        return "0";
      return "+1";
    }
    return nullptr;
  }

  void init(float *) override final
  {
    bpm_ = 96.f;
    running_ = false;
    use_host_clock_ = false;
    tick_counter_ = 0U;
    internal_tick_phase_ = 0.f;
    detect_write_ = 0U;
    detect_fill_ = 0U;
    downsample_accum_ = 0.f;
    downsample_count_ = 0U;
    analyze_countdown_ = 0U;
    lp_z_ = 0.f;
    detected_midi_ = 36.f;
    locked_midi_ = 36.f;
    have_pitch_ = false;
    phase_ = 0.f;
    env_age_ = 1e9f;
    env_vel_ = 0.f;
    gate_hold_ = false;
    triggers_ = 0U;
    for (uint32_t sampleIndex = 0; sampleIndex < kDetectSize; ++sampleIndex)
      detect_buf_[sampleIndex] = 0.f;
  }

  void reset() override final
  {
    running_ = false;
    env_age_ = 1e9f;
    env_vel_ = 0.f;
    gate_hold_ = false;
    phase_ = 0.f;
    have_pitch_ = false;
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 40.f && tempo < 300.f)
      bpm_ = tempo;
  }

  void tempo4ppqnTick(uint32_t counter) override final
  {
    use_host_clock_ = true;
    handleTick(counter);
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      running_ = false;
      gate_hold_ = false;
      return;
    }

    if (phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
        phase == k_unit_touch_phase_stationary)
    {
      running_ = true;
      if (rhythm_ == RHY_HOLD)
        gate_hold_ = true;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const float inv_sr = 1.f / getSampleRate();
    const float amp_tau = 0.035f + decay_norm_ * 0.28f;
    // One-pole toward ~180 Hz to bias AMDF toward the lowest partial.
    const float lp_coeff = fx::onePoleCoeff(180.f, getSampleRate());

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      if (!use_host_clock_)
        advanceInternalClockOneSample();

      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);
      const float mono = (live_left + live_right) * 0.5f;

      lp_z_ += lp_coeff * (mono - lp_z_);
      feedDetector(lp_z_);

      if (rhythm_ == RHY_HOLD)
      {
        gate_hold_ = running_ && have_pitch_;
        if (gate_hold_)
          locked_midi_ = detected_midi_;
      }

      const float wet = renderVoice(inv_sr, amp_tau);
      out[0] = live_left + wet * mix_;
      out[1] = live_right + wet * mix_;
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

  float debugDetectedMidi() const { return detected_midi_; }
  float debugLockedMidi() const { return locked_midi_; }
  bool debugHavePitch() const { return have_pitch_; }
  uint32_t debugTriggers() const { return triggers_; }
  uint32_t debugStepIndex() const
  {
    if (tick_counter_ == 0U)
      return 0U;
    return (tick_counter_ - 1U) % kSteps;
  }
  void debugResetCounters() { triggers_ = 0U; }
  void debugForcePitch(float midi)
  {
    detected_midi_ = midi;
    locked_midi_ = midi;
    have_pitch_ = true;
  }
  void debugTriggerStep(uint32_t step) { triggerForStep(step % kSteps); }
  void debugAnalyzeNow() { analyzePitch(); }

private:
  void advanceInternalClockOneSample()
  {
    // 4ppqn: 4 ticks per beat → 16 ticks per bar at 4/4.
    const float samples_per_tick = getSampleRate() * 60.f / (bpm_ * 4.f);
    internal_tick_phase_ += 1.f;
    if (internal_tick_phase_ >= samples_per_tick)
    {
      internal_tick_phase_ -= samples_per_tick;
      handleTick(tick_counter_ + 1U);
    }
  }

  void handleTick(uint32_t counter)
  {
    tick_counter_ = counter;
    if (!running_)
      return;

    // Latch pitch once per 16th: stable within the step, free to move across steps.
    if (have_pitch_ && rhythm_ != RHY_HOLD)
      locked_midi_ = detected_midi_;

    triggerForStep((counter == 0U) ? 0U : ((counter - 1U) % kSteps));
  }

  bool patternHit(uint32_t step) const
  {
    switch (rhythm_)
    {
    case RHY_HOLD:
      return false;
    case RHY_TRESI:
      // Tresillo 3+3+2 twice per bar: 0,3,6 | 8,11,14
      return step == 0U || step == 3U || step == 6U || step == 8U || step == 11U || step == 14U;
    case RHY_CINQ:
      // Cinquillo 2+1+2+1+2 on each half-bar.
      return step == 0U || step == 2U || step == 3U || step == 5U || step == 6U || step == 8U ||
             step == 10U || step == 11U || step == 13U || step == 14U;
    case RHY_HALF:
      return step == 0U || step == 8U;
    default:
      return false;
    }
  }

  void triggerForStep(uint32_t step)
  {
    if (rhythm_ == RHY_HOLD)
      return;
    if (!patternHit(step))
      return;
    if (!have_pitch_)
      return;

    env_age_ = 0.f;
    env_vel_ = 1.f;
    ++triggers_;
  }

  void feedDetector(float sample)
  {
    downsample_accum_ += sample;
    ++downsample_count_;
    if (downsample_count_ < kDownsample)
      return;

    const float down = downsample_accum_ * (1.f / static_cast<float>(kDownsample));
    downsample_accum_ = 0.f;
    downsample_count_ = 0U;

    detect_buf_[detect_write_] = down;
    detect_write_ = (detect_write_ + 1U) & kDetectMask;
    if (detect_fill_ < kDetectSize)
      ++detect_fill_;

    if (analyze_countdown_ > 0U)
    {
      --analyze_countdown_;
      return;
    }
    analyze_countdown_ = 48U; // ~64 ms between analyses at 6 kHz
    if (detect_fill_ >= kDetectSize)
      analyzePitch();
  }

  void analyzePitch()
  {
    // AMDF over bass-range lags at the downsampled rate.
    // 40 Hz → lag 150, 220 Hz → lag ~27.
    const uint32_t lag_min = 27U;
    const uint32_t lag_max = 150U;
    float best_cost = 1e30f;
    uint32_t best_lag = 0U;

    float energy = 0.f;
    for (uint32_t sampleIndex = 0; sampleIndex < kDetectSize; ++sampleIndex)
      energy += fx::absf(detect_buf_[sampleIndex]);
    energy *= (1.f / static_cast<float>(kDetectSize));
    if (energy < 0.004f)
    {
      have_pitch_ = false;
      return;
    }

    float costs[151];
    for (uint32_t lag = 0; lag <= lag_max; ++lag)
      costs[lag] = 1e30f;

    const uint32_t oldest = detect_write_; // next write slot == oldest sample
    for (uint32_t lag = lag_min; lag <= lag_max; ++lag)
    {
      float cost = 0.f;
      const uint32_t pairs = kDetectSize - lag;
      for (uint32_t sampleIndex = 0; sampleIndex < pairs; ++sampleIndex)
      {
        const uint32_t a = (oldest + sampleIndex) & kDetectMask;
        const uint32_t b = (oldest + sampleIndex + lag) & kDetectMask;
        cost += fx::absf(detect_buf_[a] - detect_buf_[b]);
      }
      cost *= (1.f / static_cast<float>(pairs));
      costs[lag] = cost;
      if (cost < best_cost)
      {
        best_cost = cost;
        best_lag = lag;
      }
    }

    if (best_lag == 0U || best_cost > energy * 1.35f)
    {
      have_pitch_ = false;
      return;
    }

    // Prefer the shortest lag whose trough is still "good". Pure tones make
    // 2×/3× periods look slightly cleaner than the true period when the period
    // is fractional, so compare against energy — not only against best_cost.
    const float accept = (best_cost * 5.f > energy * 0.12f) ? (best_cost * 5.f) : (energy * 0.12f);
    uint32_t chosen_lag = best_lag;
    for (uint32_t lag = lag_min; lag < best_lag; ++lag)
    {
      if (costs[lag] <= accept)
      {
        chosen_lag = lag;
        break;
      }
    }
    best_lag = chosen_lag;

    const float hz = kDetectRate / static_cast<float>(best_lag);
    float midi = 69.f + 12.f * fastlog2f(hz * (1.f / 440.f));
    midi = static_cast<float>(static_cast<int32_t>(midi + 0.5f));
    midi = fx::clip(midi, 24.f, 60.f);

    // Slow latch so the bass does not chatter between neighbours.
    if (!have_pitch_)
    {
      detected_midi_ = midi;
      locked_midi_ = midi;
      have_pitch_ = true;
      return;
    }

    if (fx::absf(midi - detected_midi_) >= 0.5f)
    {
      // Require a clear jump of at least a semitone before retargeting.
      if (fx::absf(midi - detected_midi_) >= 1.f)
        detected_midi_ = midi;
    }
  }

  float oscSample(float phase, float increment) const
  {
    switch (wave_)
    {
    case WAVE_SIN:
      return fastersinfullf(phase * kTwoPi);
    case WAVE_TRI:
    {
      const float t = phase < 0.5f ? (phase * 4.f - 1.f) : (3.f - phase * 4.f);
      return t;
    }
    case WAVE_SAW:
      return fx::blepSaw(phase, increment);
    case WAVE_SQR:
    default:
      return fx::blepPulse(phase, increment, 0.5f);
    }
  }

  float renderVoice(float inv_sr, float amp_tau)
  {
    const bool active = (rhythm_ == RHY_HOLD) ? (gate_hold_ && have_pitch_) : (env_vel_ > 0.001f);
    if (!active && env_age_ > amp_tau * 10.f)
      return 0.f;

    float amp = 0.f;
    if (rhythm_ == RHY_HOLD)
    {
      // Age-based fade only after release; while held stay full.
      if (gate_hold_)
      {
        amp = 1.f;
        env_age_ = 0.f;
      }
      else
      {
        amp = fasterexpf(-env_age_ / amp_tau);
        env_age_ += inv_sr;
      }
    }
    else
    {
      amp = fasterexpf(-env_age_ / amp_tau) * env_vel_;
      env_age_ += inv_sr;
      if (env_age_ > amp_tau * 10.f)
        env_vel_ = 0.f;
    }

    if (amp < 0.0001f)
      return 0.f;

    const float midi = locked_midi_ + static_cast<float>(octave_) * 12.f;
    const float increment = fx::noteToInc(midi, getSampleRate());
    phase_ = fx::wrap01(phase_ + increment);
    const float tone = oscSample(phase_, increment);
    // Mild low shelf so SQR/SAW do not get harsh in the sub range.
    lp_voice_ += 0.22f * (tone - lp_voice_);
    return fx::softclip(lp_voice_ * amp * 0.85f);
  }

  float detect_buf_[kDetectSize] = {};
  uint32_t detect_write_ = 0U;
  uint32_t detect_fill_ = 0U;
  float downsample_accum_ = 0.f;
  uint32_t downsample_count_ = 0U;
  uint32_t analyze_countdown_ = 0U;
  float lp_z_ = 0.f;
  float lp_voice_ = 0.f;

  float detected_midi_ = 36.f;
  float locked_midi_ = 36.f;
  bool have_pitch_ = false;

  float phase_ = 0.f;
  float env_age_ = 1e9f;
  float env_vel_ = 0.f;
  bool gate_hold_ = false;

  float bpm_ = 96.f;
  bool running_ = false;
  bool use_host_clock_ = false;
  uint32_t tick_counter_ = 0U;
  float internal_tick_phase_ = 0.f;
  uint32_t triggers_ = 0U;

  int8_t octave_ = -1;
  uint8_t wave_ = WAVE_SAW;
  uint8_t rhythm_ = RHY_TRESI;
  float decay_norm_ = 0.45f;
  float mix_ = 0.85f;
};
