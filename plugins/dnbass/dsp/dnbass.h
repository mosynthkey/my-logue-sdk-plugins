#pragma once

/*
 * File: dnbass.h
 *
 * Tempo-synced drum & bass kit for NTS-3.
 * Hold to run. X = hat roll density. Y = break / fill energy.
 * Half-time snare on 3. Top-right flick = one-bar Fill.
 * Hits lock to host 4ppqn.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class DnBass : public Processor
{
public:
  static constexpr uint32_t kSteps = 16U;
  static constexpr float kTwoPi = 6.283185307179586f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    HATS = 0U,
    BREAK,
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
    case HATS:
      hats_norm_ = param_10bit_to_f32(value);
      break;
    case BREAK:
      break_norm_ = param_10bit_to_f32(value);
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
    bpm_ = 174.f;
    running_ = false;
    use_host_clock_ = false;
    fill_timer_ = 0U;
    tick_counter_ = 0U;
    internal_tick_phase_ = 0.f;
    swing_samples_left_ = 0;
    pending_step_ = 0U;
    rng_ = 0xD8B001u;
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
    kick_hz_ = 55.f;
    snare_hz_ = 195.f;
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
    const float swing = 0.2f + swing_norm_ * 0.25f;
    return swing * 0.16f;
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

  // Half-time DnB: kick on 1 + syncopated pickups; snare on 3.
  static bool isKickSpine(uint32_t step)
  {
    return step == 0U || step == 6U || step == 10U;
  }

  static bool isSnareSpine(uint32_t step)
  {
    return step == 8U;
  }

  static bool isGhostKickSeat(uint32_t step)
  {
    return step == 2U || step == 3U || step == 12U || step == 14U;
  }

  static bool isGhostSnareSeat(uint32_t step)
  {
    return step == 4U || step == 7U || step == 11U || step == 15U;
  }

  static bool isClosedHatSeat(uint32_t step)
  {
    return true;
  }

  static bool isOpenHatSeat(uint32_t step)
  {
    return step == 2U || step == 6U || step == 10U || step == 14U;
  }

  float velocityJitter(float base)
  {
    return fx::clip01(base * (0.82f + fx::randomFloat(rng_) * 0.34f));
  }

  void fire(HitKind kind, float velocity)
  {
    const float vel = velocityJitter(velocity);
    switch (kind)
    {
    case kKick:
      kick_age_ = 0.f;
      kick_vel_ = vel;
      kick_hz_ = 48.f + tone_norm_ * 32.f;
      kick_phase_ = 0.f;
      ++main_triggers_;
      break;
    case kSnare:
      snare_age_ = 0.f;
      snare_vel_ = vel;
      snare_hz_ = 170.f + tone_norm_ * 90.f;
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
      ++ghost_triggers_; // X density probe uses hat activity
      break;
    case kHatOpen:
      hat_o_age_ = 0.f;
      hat_o_vel_ = vel;
      break;
    }
  }

  void triggerStep(uint32_t step)
  {
    const bool fill_active = fill_timer_ > 0U || break_norm_ > 0.9f;
    const float hats = hats_norm_;
    const float brk = break_norm_;

    if (isKickSpine(step))
      fire(kKick, fill_active ? 1.f : (step == 0U ? 1.f : 0.75f));
    else if (isGhostKickSeat(step))
    {
      float chance = brk * 0.45f;
      if (fill_active)
        chance = fx::clip01(chance + 0.55f);
      if (fx::randomFloat(rng_) < chance)
        fire(kKick, 0.4f + brk * 0.35f);
    }

    if (isSnareSpine(step))
      fire(kSnare, fill_active ? 1.f : 0.95f);
    else if (isGhostSnareSeat(step))
    {
      float chance = 0.1f + brk * 0.55f;
      if (fill_active)
        chance = 0.9f;
      if (fx::randomFloat(rng_) < chance)
        fire(kSnare, 0.35f + brk * 0.4f);
    }
    else if (fill_active && fx::randomFloat(rng_) < 0.75f)
      fire(kSnare, 0.45f + fx::randomFloat(rng_) * 0.35f);

    // Soft ghost texture when break energy is mid.
    if (!isKickSpine(step) && !isSnareSpine(step) && brk > 0.25f &&
        (step == 1U || step == 5U || step == 9U || step == 13U))
    {
      if (fx::randomFloat(rng_) < brk * 0.35f)
        fire(kGhost, 0.18f + brk * 0.15f);
    }

    if (isClosedHatSeat(step))
    {
      // Rolling 16ths that densify with X; near top become 32nd-feel doubles via velocity.
      float hat_chance = 0.35f + hats * 0.6f;
      if ((step % 2U) != 0U)
        hat_chance *= 0.55f + hats * 0.45f;
      if (fill_active)
        hat_chance = 0.98f;
      if (fx::randomFloat(rng_) < hat_chance)
      {
        const float accent = ((step % 4U) == 0U) ? 0.7f : 0.35f;
        fire(kHatClosed, accent + hats * 0.25f);
      }
    }

    if (isOpenHatSeat(step))
    {
      float open_chance = 0.08f + hats * 0.45f + brk * 0.1f;
      if (fill_active)
        open_chance = 0.7f;
      if (fx::randomFloat(rng_) < open_chance)
        fire(kHatOpen, 0.45f + hats * 0.3f);
    }
  }

  float renderVoices()
  {
    const float sr = getSampleRate();
    const float kick_tau = (0.04f + decay_norm_ * 0.08f) * sr;
    const float snare_tau = (0.035f + decay_norm_ * 0.07f) * sr;
    const float ghost_tau = (0.016f + decay_norm_ * 0.025f) * sr;
    const float hat_c_tau = (0.01f + decay_norm_ * 0.016f) * sr;
    const float hat_o_tau = (0.05f + decay_norm_ * 0.08f) * sr;

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

    kick_hz_ += (38.f - kick_hz_) * 0.0025f;
    kick_phase_ = fx::wrap01(kick_phase_ + kick_hz_ / sr);
    const float kick = fastersinfullf(kick_phase_ * kTwoPi) * kick_env * 1.35f;

    snare_phase_ = fx::wrap01(snare_phase_ + snare_hz_ / sr);
    const float snare_tone = fastersinfullf(snare_phase_ * kTwoPi);
    const float noise = fx::randomFloat(rng_) * 2.f - 1.f;
    // Punchy half-time snare.
    const float snare = (snare_tone * 0.28f + noise * 0.72f) * snare_env * 1.05f;

    const float ghost = noise * ghost_env * 0.38f;

    const float hat_raw = noise;
    hat_hp_ += 0.42f * (hat_raw - hat_hp_);
    const float hat_bright = hat_raw - hat_hp_;
    const float hats = hat_bright * (hat_c_env * 0.34f + hat_o_env * 0.5f);

    return fx::softclip(kick + snare + ghost + hats);
  }

  float bpm_ = 174.f;
  float hats_norm_ = 0.45f;
  float break_norm_ = 0.3f;
  float swing_norm_ = 0.3f;
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
  float kick_hz_ = 55.f;
  float snare_hz_ = 195.f;
  float hat_hp_ = 0.f;

  uint32_t tick_counter_ = 0U;
  uint32_t pending_step_ = 0U;
  uint32_t fill_timer_ = 0U;
  uint32_t rng_ = 0xD8B001u;
  uint32_t ghost_triggers_ = 0U;
  uint32_t main_triggers_ = 0U;
  int32_t swing_samples_left_ = 0;
  bool running_ = false;
  bool use_host_clock_ = false;
};
