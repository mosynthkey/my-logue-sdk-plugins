#pragma once

/*
 * File: trance.h
 *
 * Tempo-synced trance drum kit for NTS-3.
 * Hold to run. X = offbeat hat energy. Y = build / fill.
 * Four-on-the-floor + clap on 2/4. Top-right flick = one-bar Fill.
 * Hits lock to host 4ppqn.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class Trance : public Processor
{
public:
  static constexpr uint32_t kSteps = 16U;
  static constexpr float kTwoPi = 6.283185307179586f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    HATS = 0U,
    BUILD,
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
    case BUILD:
      build_norm_ = param_10bit_to_f32(value);
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
    bpm_ = 138.f;
    running_ = false;
    use_host_clock_ = false;
    fill_timer_ = 0U;
    tick_counter_ = 0U;
    internal_tick_phase_ = 0.f;
    swing_samples_left_ = 0;
    pending_step_ = 0U;
    rng_ = 0x7A7CE1u;
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
    snare_hz_ = 200.f;
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
    // Trance stays fairly straight; light shuffle only.
    const float swing = 0.1f + swing_norm_ * 0.2f;
    return swing * 0.1f;
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

  static bool isKickSpine(uint32_t step)
  {
    // Four-on-the-floor.
    return (step % 4U) == 0U;
  }

  static bool isSnareSpine(uint32_t step)
  {
    // Clap / snare on 2 and 4.
    return step == 4U || step == 12U;
  }

  static bool isClosedHatSeat(uint32_t step)
  {
    return (step % 2U) == 0U || step == 1U || step == 5U || step == 9U || step == 13U;
  }

  static bool isOpenHatSeat(uint32_t step)
  {
    // Classic trance offbeat opens.
    return step == 2U || step == 6U || step == 10U || step == 14U;
  }

  static bool isBuildSnareSeat(uint32_t step)
  {
    return step == 3U || step == 7U || step == 11U || step == 13U || step == 14U || step == 15U;
  }

  float velocityJitter(float base)
  {
    return fx::clip01(base * (0.88f + fx::randomFloat(rng_) * 0.24f));
  }

  void fire(HitKind kind, float velocity)
  {
    const float vel = velocityJitter(velocity);
    switch (kind)
    {
    case kKick:
      kick_age_ = 0.f;
      kick_vel_ = vel;
      kick_hz_ = 50.f + tone_norm_ * 28.f;
      kick_phase_ = 0.f;
      ++main_triggers_;
      break;
    case kSnare:
      snare_age_ = 0.f;
      snare_vel_ = vel;
      snare_hz_ = 185.f + tone_norm_ * 70.f;
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
      ++ghost_triggers_;
      break;
    case kHatOpen:
      hat_o_age_ = 0.f;
      hat_o_vel_ = vel;
      ++ghost_triggers_;
      break;
    }
  }

  void triggerStep(uint32_t step)
  {
    const bool fill_active = fill_timer_ > 0U || build_norm_ > 0.9f;
    const float hats = hats_norm_;
    const float build = build_norm_;

    if (isKickSpine(step))
      fire(kKick, fill_active ? 1.f : 0.95f);
    else if (fill_active && ((step % 2U) == 0U) && fx::randomFloat(rng_) < 0.4f)
      fire(kKick, 0.55f);

    if (isSnareSpine(step))
      fire(kSnare, fill_active ? 1.f : 0.9f);
    else if (isBuildSnareSeat(step))
    {
      float chance = build * 0.55f;
      if (step >= 12U)
        chance += build * 0.35f;
      if (fill_active)
        chance = 0.95f;
      if (fx::randomFloat(rng_) < chance)
        fire(kSnare, 0.35f + build * 0.45f);
    }

    // Soft gallop ghosts when hats are high.
    if (!isKickSpine(step) && !isSnareSpine(step) && hats > 0.55f && (step % 2U) != 0U)
    {
      if (fx::randomFloat(rng_) < (hats - 0.45f) * 0.6f)
        fire(kGhost, 0.15f + hats * 0.12f);
    }

    if (isClosedHatSeat(step))
    {
      float hat_chance = 0.25f + hats * 0.7f;
      if ((step % 2U) != 0U)
        hat_chance *= 0.5f + hats * 0.5f;
      if (fill_active)
        hat_chance = 0.95f;
      if (fx::randomFloat(rng_) < hat_chance)
        fire(kHatClosed, ((step % 4U) == 0U) ? 0.55f : 0.32f);
    }

    if (isOpenHatSeat(step))
    {
      // Offbeat opens are the trance identity — always present, louder with X.
      float open_chance = 0.55f + hats * 0.45f;
      if (fill_active)
        open_chance = 0.95f;
      if (fx::randomFloat(rng_) < open_chance)
        fire(kHatOpen, 0.55f + hats * 0.35f);
    }
  }

  float renderVoices()
  {
    const float sr = getSampleRate();
    const float kick_tau = (0.05f + decay_norm_ * 0.09f) * sr;
    const float snare_tau = (0.04f + decay_norm_ * 0.07f) * sr;
    const float ghost_tau = (0.018f + decay_norm_ * 0.025f) * sr;
    const float hat_c_tau = (0.012f + decay_norm_ * 0.02f) * sr;
    const float hat_o_tau = (0.09f + decay_norm_ * 0.14f) * sr;

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

    // Punchy four-on-floor kick with quick pitch drop.
    kick_hz_ += (36.f - kick_hz_) * 0.0028f;
    kick_phase_ = fx::wrap01(kick_phase_ + kick_hz_ / sr);
    const float kick = fastersinfullf(kick_phase_ * kTwoPi) * kick_env * 1.4f;

    snare_phase_ = fx::wrap01(snare_phase_ + snare_hz_ / sr);
    const float snare_tone = fastersinfullf(snare_phase_ * kTwoPi);
    const float noise = fx::randomFloat(rng_) * 2.f - 1.f;
    // Clappier snare (more noise, less tone).
    const float snare = (snare_tone * 0.18f + noise * 0.82f) * snare_env * 0.95f;

    const float ghost = noise * ghost_env * 0.3f;

    const float hat_raw = noise;
    hat_hp_ += 0.38f * (hat_raw - hat_hp_);
    const float hat_bright = hat_raw - hat_hp_;
    // Open hats are louder / longer — the trance offbeat.
    const float hats = hat_bright * (hat_c_env * 0.28f + hat_o_env * 0.62f);

    return fx::softclip(kick + snare + ghost + hats);
  }

  float bpm_ = 138.f;
  float hats_norm_ = 0.5f;
  float build_norm_ = 0.25f;
  float swing_norm_ = 0.2f;
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
  float snare_hz_ = 200.f;
  float hat_hp_ = 0.f;

  uint32_t tick_counter_ = 0U;
  uint32_t pending_step_ = 0U;
  uint32_t fill_timer_ = 0U;
  uint32_t rng_ = 0x7A7CE1u;
  uint32_t ghost_triggers_ = 0U;
  uint32_t main_triggers_ = 0U;
  int32_t swing_samples_left_ = 0;
  bool running_ = false;
  bool use_host_clock_ = false;
};
