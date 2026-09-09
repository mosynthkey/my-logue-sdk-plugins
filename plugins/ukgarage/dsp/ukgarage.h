#pragma once

/*
 * File: ukgarage.h
 *
 * Tempo-synced 2-step UK Garage kit for NTS-3.
 * Hold to run. X = ghost-note density. Y = fill / hat energy.
 * Top-right flick = one-bar Fill. See RESEARCH.md.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class UKGarage : public Processor
{
public:
  static constexpr uint32_t kSteps = 16U;
  static constexpr float kTwoPi = 6.283185307179586f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    GHOST = 0U,
    FILL,
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
    case GHOST:
      ghost_norm_ = param_10bit_to_f32(value);
      break;
    case FILL:
      fill_norm_ = param_10bit_to_f32(value);
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
    bpm_ = 134.f;
    running_ = false;
    use_host_clock_ = false;
    fill_timer_ = 0U;
    tick_counter_ = 0U;
    internal_tick_phase_ = 0.f;
    swing_samples_left_ = 0;
    pending_step_ = 0U;
    rng_ = 0xC0FFEEu;
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

  // Lock phrase steps to the host 4ppqn grid (16ths). Touch only gates sound.
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
      // Gate only — do not reset the clock or fire from the tap moment.
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
      // Free-run even while the pad is up so the next hold joins the same grid.
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

  // Host probes
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
    snare_hz_ = 190.f;
    hat_hp_ = 0.f;
  }

  float samplesPerSixteenth() const
  {
    if (bpm_ <= 0.f)
      return 0.f;
    return getSampleRate() * 60.f / (bpm_ * 4.f);
  }

  // Map SWING toward classic UKG 16th shuffle (~Ableton 63–67% feel).
  float swingDelayFraction() const
  {
    const float swing = 0.45f + swing_norm_ * 0.35f;
    return swing * 0.28f;
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
    // Delay odd 16ths for shuffle; even steps stay on the beat grid.
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

  static bool isKickSpine(uint32_t step)
  {
    // Classic 2-step: beat 1 and the "and" of 3.
    return step == 0U || step == 10U;
  }

  static bool isSnareSpine(uint32_t step)
  {
    return step == 4U || step == 12U;
  }

  // Soft ghost seats: 16th before snares, answers after kicks, mid-bar push.
  static bool isGhostSeat(uint32_t step)
  {
    return step == 1U || step == 3U || step == 6U || step == 7U || step == 9U || step == 11U ||
           step == 14U || step == 15U;
  }

  static bool isClosedHatSeat(uint32_t step)
  {
    // Swung closed hats land on even 16ths plus a few offbeats.
    return (step % 2U) == 0U || step == 1U || step == 5U || step == 9U || step == 13U;
  }

  static bool isOpenHatSeat(uint32_t step)
  {
    return step == 2U || step == 6U || step == 10U || step == 14U;
  }

  float velocityJitter(float base)
  {
    return fx::clip01(base * (0.82f + fx::randomFloat(rng_) * 0.36f));
  }

  void fire(HitKind kind, float velocity)
  {
    const float vel = velocityJitter(velocity);
    switch (kind)
    {
    case kKick:
      kick_age_ = 0.f;
      kick_vel_ = vel;
      kick_hz_ = 48.f + tone_norm_ * 36.f;
      kick_phase_ = 0.f;
      ++main_triggers_;
      break;
    case kSnare:
      snare_age_ = 0.f;
      snare_vel_ = vel;
      snare_hz_ = 165.f + tone_norm_ * 90.f;
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
    const bool fill_active = fill_timer_ > 0U || fill_norm_ > 0.88f;
    const float ghost = ghost_norm_;
    const float fill = fill_norm_;

    // --- Backbone: always present so the 2-step identity stays clear ---
    if (isKickSpine(step))
      fire(kKick, fill_active ? 1.f : 0.95f);
    else if (fill_active && ((step % 2U) == 0U) && fx::randomFloat(rng_) < 0.55f)
      fire(kKick, 0.72f);
    else if (fill > 0.55f && (step == 2U || step == 8U || step == 14U) &&
             fx::randomFloat(rng_) < (fill - 0.45f))
      fire(kKick, 0.55f);

    if (isSnareSpine(step))
      fire(kSnare, fill_active ? 1.f : 0.92f);
    else if (fill_active)
    {
      // Fill: snare rolls on remaining 16ths.
      if (fx::randomFloat(rng_) < 0.85f)
        fire(kSnare, 0.55f + fx::randomFloat(rng_) * 0.35f);
    }
    else if (fill > 0.7f && (step == 5U || step == 13U) && fx::randomFloat(rng_) < fill * 0.5f)
      fire(kSnare, 0.45f);

    // --- Ghost notes: the UKG groove glue ---
    // Keep velocity in the 20–40% band of a main hit unless Fill pushes louder.
    if (!isSnareSpine(step) && !isKickSpine(step) && isGhostSeat(step))
    {
      float ghost_chance = ghost * 0.55f;
      if (step == 3U || step == 11U)
        ghost_chance += ghost * 0.35f; // pull into the snare
      if (step == 1U || step == 9U)
        ghost_chance += ghost * 0.15f;
      ghost_chance += fill * 0.12f;
      if (fill_active)
        ghost_chance = fx::clip01(ghost_chance + 0.45f);

      if (fx::randomFloat(rng_) < ghost_chance)
      {
        const float ghost_vel = 0.18f + ghost * 0.18f + fill * 0.08f;
        fire(kGhost, fx::clip01(ghost_vel));
      }
    }

    // Soft kick ghost double in the second half of the bar (forward push).
    if (step == 8U && ghost > 0.35f && !fill_active && fx::randomFloat(rng_) < ghost * 0.4f)
      fire(kKick, 0.22f + ghost * 0.12f);

    // --- Hats ---
    if (isClosedHatSeat(step))
    {
      float hat_chance = 0.35f + fill * 0.55f + ghost * 0.15f;
      if ((step % 2U) != 0U)
        hat_chance *= 0.65f + ghost * 0.35f;
      if (fill_active)
        hat_chance = 0.95f;
      if (fx::randomFloat(rng_) < hat_chance)
      {
        const float accent = ((step % 4U) == 0U) ? 0.7f : 0.4f;
        fire(kHatClosed, accent + fill * 0.2f);
      }
    }

    if (isOpenHatSeat(step))
    {
      float open_chance = 0.08f + fill * 0.45f;
      if (fill_active)
        open_chance = 0.7f;
      if (fx::randomFloat(rng_) < open_chance)
        fire(kHatOpen, 0.45f + fill * 0.3f);
    }
  }

  float renderVoices()
  {
    const float sr = getSampleRate();
    const float kick_tau = (0.045f + decay_norm_ * 0.09f) * sr;
    const float snare_tau = (0.035f + decay_norm_ * 0.07f) * sr;
    const float ghost_tau = (0.018f + decay_norm_ * 0.025f) * sr;
    const float hat_c_tau = (0.012f + decay_norm_ * 0.02f) * sr;
    const float hat_o_tau = (0.06f + decay_norm_ * 0.1f) * sr;

    // Age-based envelopes (safer than per-sample fasterexpf coeffs near 1).
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

    // Short dumpier modern UKG kick: pitch drop into a sine body.
    kick_hz_ += (38.f - kick_hz_) * 0.0022f;
    kick_phase_ = fx::wrap01(kick_phase_ + kick_hz_ / sr);
    const float kick = fastersinfullf(kick_phase_ * kTwoPi) * kick_env * 1.35f;

    snare_phase_ = fx::wrap01(snare_phase_ + snare_hz_ / sr);
    const float snare_tone = fastersinfullf(snare_phase_ * kTwoPi);
    const float noise = fx::randomFloat(rng_) * 2.f - 1.f;
    const float snare = (snare_tone * 0.32f + noise * 0.68f) * snare_env * 0.95f;

    // Ghost = pitched-up thin rim / soft snare; stays under the backbone.
    const float ghost = noise * ghost_env * 0.42f +
                        fastersinfullf(fx::wrap01(snare_phase_ * 1.7f) * kTwoPi) * ghost_env * 0.18f;

    // Cheap high-pass-ish hats via one-pole residual.
    const float hat_raw = noise;
    hat_hp_ += 0.35f * (hat_raw - hat_hp_);
    const float hat_bright = hat_raw - hat_hp_;
    const float hats = hat_bright * (hat_c_env * 0.32f + hat_o_env * 0.48f);

    return fx::softclip(kick + snare + ghost + hats);
  }

  float bpm_ = 134.f;
  float ghost_norm_ = 0.35f;
  float fill_norm_ = 0.25f;
  float swing_norm_ = 0.55f;
  float tone_norm_ = 0.45f;
  float decay_norm_ = 0.45f;
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
  float snare_hz_ = 190.f;
  float hat_hp_ = 0.f;

  uint32_t tick_counter_ = 0U;
  uint32_t pending_step_ = 0U;
  uint32_t fill_timer_ = 0U;
  uint32_t rng_ = 0xC0FFEEu;
  uint32_t ghost_triggers_ = 0U;
  uint32_t main_triggers_ = 0U;
  int32_t swing_samples_left_ = 0;
  bool running_ = false;
  bool use_host_clock_ = false;
};
