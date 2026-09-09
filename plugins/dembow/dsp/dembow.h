#pragma once

/*
 * File: dembow.h
 *
 * Tempo-synced reggaeton dembow kit for NTS-3.
 * Hold to run. X = rim / cha density. Y = percussion / fill energy.
 * Top-right flick = one-bar Fill. Hits lock to host 4ppqn.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class Dembow : public Processor
{
public:
  static constexpr uint32_t kSteps = 16U;
  static constexpr float kTwoPi = 6.283185307179586f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    RIM = 0U,
    PERC,
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
    case RIM:
      rim_norm_ = param_10bit_to_f32(value);
      break;
    case PERC:
      perc_norm_ = param_10bit_to_f32(value);
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
    bpm_ = 96.f;
    running_ = false;
    use_host_clock_ = false;
    fill_timer_ = 0U;
    tick_counter_ = 0U;
    internal_tick_phase_ = 0.f;
    swing_samples_left_ = 0;
    pending_step_ = 0U;
    rng_ = 0xDEB0AAu;
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
      out[0] = fx::mix(in[0], wet, mix_);
      out[1] = fx::mix(in[1], wet, mix_);
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
    kick_hz_ = 52.f;
    snare_hz_ = 210.f;
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
    const float swing = 0.25f + swing_norm_ * 0.25f;
    return swing * 0.18f;
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

  // Classic dembow kick seats (boom ... boom-ch feel).
  static bool isKickSpine(uint32_t step)
  {
    return step == 0U || step == 6U || step == 8U || step == 14U;
  }

  // Main "cha" snares on 2/4 plus the dembow offbeat answers.
  static bool isSnareSpine(uint32_t step)
  {
    return step == 4U || step == 12U;
  }

  static bool isChaSeat(uint32_t step)
  {
    return step == 7U || step == 15U;
  }

  static bool isRimSeat(uint32_t step)
  {
    return step == 2U || step == 3U || step == 5U || step == 9U || step == 11U || step == 13U;
  }

  static bool isClosedHatSeat(uint32_t step)
  {
    return step == 1U || step == 3U || step == 5U || step == 9U || step == 11U || step == 13U;
  }

  static bool isOpenHatSeat(uint32_t step)
  {
    return step == 2U || step == 10U;
  }

  float velocityJitter(float base)
  {
    return fx::clip01(base * (0.84f + fx::randomFloat(rng_) * 0.3f));
  }

  void fire(HitKind kind, float velocity)
  {
    const float vel = velocityJitter(velocity);
    switch (kind)
    {
    case kKick:
      kick_age_ = 0.f;
      kick_vel_ = vel;
      kick_hz_ = 46.f + tone_norm_ * 30.f;
      kick_phase_ = 0.f;
      ++main_triggers_;
      break;
    case kSnare:
      snare_age_ = 0.f;
      snare_vel_ = vel;
      snare_hz_ = 190.f + tone_norm_ * 70.f;
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
    const bool fill_active = fill_timer_ > 0U || perc_norm_ > 0.9f;
    const float rim = rim_norm_;
    const float perc = perc_norm_;

    if (isKickSpine(step))
      fire(kKick, fill_active ? 1.f : (step == 0U || step == 8U ? 0.98f : 0.78f));
    else if (fill_active && ((step % 2U) == 0U) && fx::randomFloat(rng_) < 0.45f)
      fire(kKick, 0.6f);

    if (isSnareSpine(step))
      fire(kSnare, fill_active ? 1.f : 0.92f);
    else if (isChaSeat(step))
      fire(kSnare, fill_active ? 0.85f : 0.7f + rim * 0.2f);
    else if (fill_active && fx::randomFloat(rng_) < 0.75f)
      fire(kSnare, 0.45f + fx::randomFloat(rng_) * 0.35f);

    if (isRimSeat(step))
    {
      float rim_chance = 0.15f + rim * 0.7f;
      if (step == 3U || step == 11U)
        rim_chance += rim * 0.2f;
      if (fill_active)
        rim_chance = fx::clip01(rim_chance + 0.35f);
      if (fx::randomFloat(rng_) < rim_chance)
        fire(kGhost, fx::clip01(0.22f + rim * 0.25f));
    }

    if (isClosedHatSeat(step))
    {
      float hat_chance = 0.2f + perc * 0.65f + rim * 0.1f;
      if (fill_active)
        hat_chance = 0.92f;
      if (fx::randomFloat(rng_) < hat_chance)
        fire(kHatClosed, 0.35f + perc * 0.25f);
    }

    if (isOpenHatSeat(step))
    {
      float open_chance = 0.1f + perc * 0.5f;
      if (fill_active)
        open_chance = 0.7f;
      if (fx::randomFloat(rng_) < open_chance)
        fire(kHatOpen, 0.5f + perc * 0.25f);
    }
  }

  float renderVoices()
  {
    const float sr = getSampleRate();
    const float kick_tau = (0.05f + decay_norm_ * 0.1f) * sr;
    const float snare_tau = (0.03f + decay_norm_ * 0.06f) * sr;
    const float ghost_tau = (0.016f + decay_norm_ * 0.02f) * sr;
    const float hat_c_tau = (0.012f + decay_norm_ * 0.018f) * sr;
    const float hat_o_tau = (0.08f + decay_norm_ * 0.12f) * sr;

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

    kick_hz_ += (36.f - kick_hz_) * 0.002f;
    kick_phase_ = fx::wrap01(kick_phase_ + kick_hz_ / sr);
    const float kick = fastersinfullf(kick_phase_ * kTwoPi) * kick_env * 1.38f;

    snare_phase_ = fx::wrap01(snare_phase_ + snare_hz_ / sr);
    const float snare_tone = fastersinfullf(snare_phase_ * kTwoPi);
    const float noise = fx::randomFloat(rng_) * 2.f - 1.f;
    // Brighter / shorter "cha" snare.
    const float snare = (snare_tone * 0.22f + noise * 0.78f) * snare_env * 1.05f;

    // Rim / woodblock-ish cha texture.
    const float rim =
        fastersinfullf(fx::wrap01(snare_phase_ * 2.4f) * kTwoPi) * ghost_env * 0.55f + noise * ghost_env * 0.2f;

    const float hat_raw = noise;
    hat_hp_ += 0.4f * (hat_raw - hat_hp_);
    const float hat_bright = hat_raw - hat_hp_;
    const float hats = hat_bright * (hat_c_env * 0.3f + hat_o_env * 0.55f);

    return fx::softclip(kick + snare + rim + hats);
  }

  float bpm_ = 96.f;
  float rim_norm_ = 0.45f;
  float perc_norm_ = 0.3f;
  float swing_norm_ = 0.35f;
  float tone_norm_ = 0.5f;
  float decay_norm_ = 0.4f;
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
  float kick_hz_ = 52.f;
  float snare_hz_ = 210.f;
  float hat_hp_ = 0.f;

  uint32_t tick_counter_ = 0U;
  uint32_t pending_step_ = 0U;
  uint32_t fill_timer_ = 0U;
  uint32_t rng_ = 0xDEB0AAu;
  uint32_t ghost_triggers_ = 0U;
  uint32_t main_triggers_ = 0U;
  int32_t swing_samples_left_ = 0;
  bool running_ = false;
  bool use_host_clock_ = false;
};
