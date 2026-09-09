#pragma once

/*
 * File: footwork.h
 *
 * Tempo-synced Chicago footwork / juke kit for NTS-3.
 * Hold to run. X = kick stutter density. Y = snare roll / fill energy.
 * Top-right flick = one-bar Fill. Hits lock to host 4ppqn.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class Footwork : public Processor
{
public:
  static constexpr uint32_t kSteps = 16U;
  static constexpr float kTwoPi = 6.283185307179586f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    STUT = 0U,
    ROLL,
    MIX,
    SWING,
    TONE,
    DEC,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case STUT:
      stut_norm_ = param_10bit_to_f32(value);
      break;
    case ROLL:
      roll_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case SWING:
      swing_norm_ = param_10bit_to_f32(value);
      break;
    case TONE:
      tone_norm_ = param_10bit_to_f32(value);
      break;
    case DEC:
      decay_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *) override final
  {
    bpm_ = 160.f;
    running_ = false;
    use_host_clock_ = false;
    fill_timer_ = 0U;
    tick_counter_ = 0U;
    internal_tick_phase_ = 0.f;
    swing_samples_left_ = 0;
    pending_step_ = 0U;
    rng_ = 0xF007AAu;
    resetVoices();
  }

  void reset() override final
  {
    running_ = false;
    fill_timer_ = 0U;
    swing_samples_left_ = 0;
    resetVoices();
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

  void touchEvent(uint8_t, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      running_ = false;
      swing_samples_left_ = 0;
      return;
    }

    if (phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
        phase == k_unit_touch_phase_stationary)
    {
      running_ = true;
      if (phase == k_unit_touch_phase_began && x > 760U && y > 760U)
        fill_timer_ = kSteps;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    (void)raw;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      if (!use_host_clock_)
        advanceInternalClockOneSample();
      advancePendingSwing();

      const float wet = renderVoices();
      out[0] = in[0] + wet * mix_;
      out[1] = in[1] + wet * mix_;
      in += 2;
      out += 2;
    }
  }

  uint32_t debugStepIndex() const
  {
    if (tick_counter_ == 0U)
      return 0U;
    return (tick_counter_ - 1U) % kSteps;
  }
  uint32_t debugGhostTriggers() const { return ghost_triggers_; }
  uint32_t debugMainTriggers() const { return main_triggers_; }
  uint32_t debugTickCounter() const { return tick_counter_; }
  void debugResetCounters()
  {
    ghost_triggers_ = 0U;
    main_triggers_ = 0U;
  }
  void debugForceRun() { running_ = true; }
  void debugTriggerStep(uint32_t step) { triggerStep(step); }

private:
  enum HitKind : uint8_t
  {
    kKick = 0U,
    kSnare,
    kGhost,
    kHatClosed,
    kHatOpen
  };

  void resetVoices()
  {
    kick_age_ = 1e9f;
    snare_age_ = 1e9f;
    ghost_age_ = 1e9f;
    hat_c_age_ = 1e9f;
    hat_o_age_ = 1e9f;
    kick_vel_ = 0.f;
    snare_vel_ = 0.f;
    ghost_vel_ = 0.f;
    hat_c_vel_ = 0.f;
    hat_o_vel_ = 0.f;
    kick_phase_ = 0.f;
    snare_phase_ = 0.f;
    kick_hz_ = 60.f;
    snare_hz_ = 220.f;
    hat_hp_ = 0.f;
  }

  float samplesPerSixteenth() const
  {
    if (bpm_ <= 0.f)
      return 0.f;
    return getSampleRate() * 60.f / (bpm_ * 4.f);
  }

  float swingDelayFraction() const
  {
    const float swing = 0.15f + swing_norm_ * 0.2f;
    return swing * 0.12f;
  }

  void emitStep(uint32_t step)
  {
    triggerStep(step);
    if (fill_timer_ > 0U)
      --fill_timer_;
  }

  void handleTick(uint32_t counter)
  {
    tick_counter_ = counter;
    if (!running_)
      return;

    const uint32_t step_index = (counter - 1U) % kSteps;
    const float delay_frac = swingDelayFraction();
    if ((step_index & 1U) != 0U && delay_frac > 0.001f)
    {
      pending_step_ = step_index;
      swing_samples_left_ = static_cast<int32_t>(samplesPerSixteenth() * delay_frac);
      if (swing_samples_left_ < 1)
        emitStep(step_index);
      return;
    }

    emitStep(step_index);
  }

  void advanceInternalClockOneSample()
  {
    const float samples_per_tick = samplesPerSixteenth();
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

  void advancePendingSwing()
  {
    if (swing_samples_left_ <= 0)
      return;
    --swing_samples_left_;
    if (swing_samples_left_ == 0)
      emitStep(pending_step_);
  }

  // Sparse spine; stutters fill the gaps via X.
  static bool isKickSpine(uint32_t step)
  {
    return step == 0U || step == 3U || step == 8U || step == 11U;
  }

  static bool isKickStutterSeat(uint32_t step)
  {
    return step == 1U || step == 2U || step == 5U || step == 6U || step == 9U || step == 10U ||
           step == 13U || step == 14U;
  }

  static bool isSnareSpine(uint32_t step)
  {
    return step == 4U || step == 12U;
  }

  static bool isRollSeat(uint32_t step)
  {
    return step == 5U || step == 6U || step == 7U || step == 13U || step == 14U || step == 15U;
  }

  static bool isClosedHatSeat(uint32_t step)
  {
    return true;
  }

  static bool isOpenHatSeat(uint32_t step)
  {
    return step == 7U || step == 15U;
  }

  float velocityJitter(float base)
  {
    return fx::clip01(base * (0.8f + fx::randomFloat(rng_) * 0.38f));
  }

  void fire(HitKind kind, float velocity)
  {
    const float vel = velocityJitter(velocity);
    switch (kind)
    {
    case kKick:
      kick_age_ = 0.f;
      kick_vel_ = vel;
      kick_hz_ = 55.f + tone_norm_ * 40.f;
      kick_phase_ = 0.f;
      ++main_triggers_;
      break;
    case kSnare:
      snare_age_ = 0.f;
      snare_vel_ = vel;
      snare_hz_ = 200.f + tone_norm_ * 100.f;
      snare_phase_ = 0.f;
      ++main_triggers_;
      break;
    case kGhost:
      ghost_age_ = 0.f;
      ghost_vel_ = vel;
      ++ghost_triggers_;
      break;
    case kHatClosed:
      hat_c_age_ = 0.f;
      hat_c_vel_ = vel;
      break;
    case kHatOpen:
      hat_o_age_ = 0.f;
      hat_o_vel_ = vel;
      break;
    }
  }

  void triggerStep(uint32_t step)
  {
    const bool fill_active = fill_timer_ > 0U || roll_norm_ > 0.9f;
    const float stut = stut_norm_;
    const float roll = roll_norm_;

    if (isKickSpine(step))
      fire(kKick, fill_active ? 1.f : 0.95f);
    else if (isKickStutterSeat(step))
    {
      float stut_chance = stut * 0.75f;
      if ((step % 4U) == 1U || (step % 4U) == 2U)
        stut_chance += stut * 0.2f;
      if (fill_active)
        stut_chance = fx::clip01(stut_chance + 0.55f);
      if (fx::randomFloat(rng_) < stut_chance)
      {
        fire(kKick, 0.45f + stut * 0.4f);
        ++ghost_triggers_; // count stutter extras for offline density probe
      }
    }

    if (isSnareSpine(step))
      fire(kSnare, fill_active ? 1.f : 0.9f);
    else if (isRollSeat(step))
    {
      float roll_chance = roll * 0.55f;
      if (step == 6U || step == 7U || step == 14U || step == 15U)
        roll_chance += roll * 0.35f;
      if (fill_active)
        roll_chance = 0.95f;
      if (fx::randomFloat(rng_) < roll_chance)
        fire(kSnare, 0.35f + roll * 0.45f + fx::randomFloat(rng_) * 0.2f);
    }

    if (isClosedHatSeat(step))
    {
      float hat_chance = 0.55f + roll * 0.4f + stut * 0.1f;
      if (fill_active)
        hat_chance = 0.98f;
      if (fx::randomFloat(rng_) < hat_chance)
        fire(kHatClosed, ((step % 2U) == 0U) ? 0.55f : 0.32f);
    }

    if (isOpenHatSeat(step) && fx::randomFloat(rng_) < (0.15f + roll * 0.5f))
      fire(kHatOpen, 0.45f + roll * 0.3f);
  }

  float renderVoices()
  {
    const float sr = getSampleRate();
    // Short punchy bodies suit ~160 BPM footwork.
    const float kick_tau = (0.028f + decay_norm_ * 0.05f) * sr;
    const float snare_tau = (0.022f + decay_norm_ * 0.04f) * sr;
    const float ghost_tau = (0.014f + decay_norm_ * 0.02f) * sr;
    const float hat_c_tau = (0.01f + decay_norm_ * 0.015f) * sr;
    const float hat_o_tau = (0.045f + decay_norm_ * 0.07f) * sr;

    const float kick_env = (kick_age_ < kick_tau * 8.f) ? fasterexpf(-kick_age_ / kick_tau) * kick_vel_ : 0.f;
    const float snare_env =
        (snare_age_ < snare_tau * 8.f) ? fasterexpf(-snare_age_ / snare_tau) * snare_vel_ : 0.f;
    const float ghost_env =
        (ghost_age_ < ghost_tau * 8.f) ? fasterexpf(-ghost_age_ / ghost_tau) * ghost_vel_ : 0.f;
    const float hat_c_env =
        (hat_c_age_ < hat_c_tau * 8.f) ? fasterexpf(-hat_c_age_ / hat_c_tau) * hat_c_vel_ : 0.f;
    const float hat_o_env =
        (hat_o_age_ < hat_o_tau * 8.f) ? fasterexpf(-hat_o_age_ / hat_o_tau) * hat_o_vel_ : 0.f;

    kick_age_ += 1.f;
    snare_age_ += 1.f;
    ghost_age_ += 1.f;
    hat_c_age_ += 1.f;
    hat_o_age_ += 1.f;

    kick_hz_ += (42.f - kick_hz_) * 0.0035f;
    kick_phase_ = fx::wrap01(kick_phase_ + kick_hz_ / sr);
    const float kick = fastersinfullf(kick_phase_ * kTwoPi) * kick_env * 1.25f;

    snare_phase_ = fx::wrap01(snare_phase_ + snare_hz_ / sr);
    const float snare_tone = fastersinfullf(snare_phase_ * kTwoPi);
    const float noise = fx::randomFloat(rng_) * 2.f - 1.f;
    const float snare = (snare_tone * 0.2f + noise * 0.8f) * snare_env * 1.05f;

    const float ghost = noise * ghost_env * 0.35f;

    const float hat_raw = noise;
    hat_hp_ += 0.45f * (hat_raw - hat_hp_);
    const float hat_bright = hat_raw - hat_hp_;
    const float hats = hat_bright * (hat_c_env * 0.34f + hat_o_env * 0.5f);

    return fx::softclip(kick + snare + ghost + hats);
  }

  float bpm_ = 160.f;
  float stut_norm_ = 0.45f;
  float roll_norm_ = 0.3f;
  float swing_norm_ = 0.25f;
  float tone_norm_ = 0.55f;
  float decay_norm_ = 0.35f;
  float mix_ = 1.f;
  float internal_tick_phase_ = 0.f;

  float kick_age_ = 1e9f;
  float snare_age_ = 1e9f;
  float ghost_age_ = 1e9f;
  float hat_c_age_ = 1e9f;
  float hat_o_age_ = 1e9f;
  float kick_vel_ = 0.f;
  float snare_vel_ = 0.f;
  float ghost_vel_ = 0.f;
  float hat_c_vel_ = 0.f;
  float hat_o_vel_ = 0.f;
  float kick_phase_ = 0.f;
  float snare_phase_ = 0.f;
  float kick_hz_ = 60.f;
  float snare_hz_ = 220.f;
  float hat_hp_ = 0.f;

  uint32_t tick_counter_ = 0U;
  uint32_t pending_step_ = 0U;
  uint32_t fill_timer_ = 0U;
  uint32_t rng_ = 0xF007AAu;
  uint32_t ghost_triggers_ = 0U;
  uint32_t main_triggers_ = 0U;
  int32_t swing_samples_left_ = 0;
  bool running_ = false;
  bool use_host_clock_ = false;
};
