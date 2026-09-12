#pragma once

/*
 * File: drums.h
 *
 * Tempo-synced multi-genre drum kit for NTS-3.
 * Hold to run. GENRE selects the pattern; KIT selects the voice feel
 * (AUTO follows GENRE). X = density, Y = fill energy.
 * Top-right flick = one-bar snare-roll Fill.
 * Hits lock to host 4ppqn. Trap808 stays a separate specialised unit.
 * Kits stay synthetic VoiceFeel tables — no sample banks (capacity).
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class Drums : public Processor
{
public:
  static constexpr uint32_t kSteps = 16U;
  static constexpr float kTwoPi = 6.283185307179586f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    DENS = 0U,
    FILL,
    MIX,
    GENRE,
    KIT,
    SWING,
    TONE,
    DEC,
    NUM_PARAMS
  };

  enum Genre : int32_t
  {
    GENRE_TRANCE = 0,
    GENRE_DNB,
    GENRE_BREAK,
    GENRE_UKG,
    GENRE_BOOM,
    GENRE_DEMBOW,
    GENRE_FOOT,
    NUM_GENRES
  };

  // AUTO follows GENRE; other values pick a VoiceFeel independently.
  enum Kit : int32_t
  {
    KIT_AUTO = 0,
    KIT_TRANCE,
    KIT_DNB,
    KIT_BREAK,
    KIT_UKG,
    KIT_BOOM,
    KIT_DEMBOW,
    KIT_FOOT,
    NUM_KITS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case DENS:
      dens_norm_ = param_10bit_to_f32(value);
      break;
    case FILL:
      fill_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case GENRE:
    {
      const int32_t next = (value < 0) ? 0 : ((value >= NUM_GENRES) ? (NUM_GENRES - 1) : value);
      if (next != genre_)
      {
        genre_ = next;
        applyGenreDefaults();
      }
      break;
    }
    case KIT:
      kit_ = (value < 0) ? 0 : ((value >= NUM_KITS) ? (NUM_KITS - 1) : value);
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

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    static const char *genre_names[NUM_GENRES] = {"TRNC", "DNB", "BRK", "UKG", "BOOM", "DEM", "FOOT"};
    static const char *kit_names[NUM_KITS] = {"AUTO", "TRNC", "DNB", "BRK", "UKG", "BOOM", "DEM", "FOOT"};
    if (index == GENRE && value >= 0 && value < NUM_GENRES)
      return genre_names[value];
    if (index == KIT && value >= 0 && value < NUM_KITS)
      return kit_names[value];
    return nullptr;
  }

  void init(float *) override final
  {
    genre_ = GENRE_TRANCE;
    kit_ = KIT_AUTO;
    applyGenreDefaults();
    dens_norm_ = 0.44f;
    fill_norm_ = 0.27f;
    swing_norm_ = 0.34f;
    tone_norm_ = 0.45f;
    decay_norm_ = 0.44f;
    mix_ = 1.f;
    running_ = false;
    use_host_clock_ = false;
    fill_corner_latched_ = false;
    fill_timer_ = 0U;
    tick_counter_ = 0U;
    internal_tick_phase_ = 0.f;
    swing_samples_left_ = 0;
    pending_step_ = 0U;
    rng_ = 0xD4A55u;
    resetVoices();
  }

  void reset() override final
  {
    running_ = false;
    fill_timer_ = 0U;
    fill_corner_latched_ = false;
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
      fill_corner_latched_ = false;
      swing_samples_left_ = 0;
      return;
    }

    if (phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
        phase == k_unit_touch_phase_stationary)
    {
      running_ = true;
      const bool in_fill_corner = (x > 760U && y > 760U);
      // Began in corner, or slide into corner while held → one-bar snare Fill.
      if (in_fill_corner && (phase == k_unit_touch_phase_began || !fill_corner_latched_))
        fill_timer_ = kSteps;
      fill_corner_latched_ = in_fill_corner;
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
  int32_t debugGenre() const { return genre_; }
  int32_t debugKit() const { return kit_; }
  int32_t debugActiveKit() const { return activeKitIndex(); }
  uint32_t debugFillTimer() const { return fill_timer_; }
  void debugResetCounters()
  {
    ghost_triggers_ = 0U;
    main_triggers_ = 0U;
  }
  void debugForceRun() { running_ = true; }
  void debugTriggerStep(uint32_t step) { triggerStep(step); }
  void debugEmitStep(uint32_t step) { emitStep(step); }

private:
  enum HitKind : uint8_t
  {
    kKick = 0U,
    kSnare,
    kGhost,
    kHatClosed,
    kHatOpen
  };

  struct VoiceFeel
  {
    float kick_tau0;
    float kick_tau1;
    float snare_tau0;
    float snare_tau1;
    float ghost_tau0;
    float ghost_tau1;
    float hat_c_tau0;
    float hat_c_tau1;
    float hat_o_tau0;
    float hat_o_tau1;
    float kick_target_hz;
    float kick_glide;
    float kick_gain;
    float snare_tone;
    float snare_noise;
    float snare_gain;
    float ghost_noise;
    float ghost_tone;
    float ghost_tone_ratio;
    float hat_hp_coeff;
    float hat_c_gain;
    float hat_o_gain;
  };

  void applyGenreDefaults()
  {
    // Suggested internal-clock BPM when host is absent.
    if (!use_host_clock_)
    {
      static const float kDefaultBpm[NUM_GENRES] = {138.f, 174.f, 174.f, 134.f, 90.f, 96.f, 160.f};
      bpm_ = kDefaultBpm[genre_];
    }
  }

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
    // Base + scale follow each genre kit's shuffle character.
    float base = 0.2f;
    float scale = 0.25f;
    float amount = 0.16f;
    switch (genre_)
    {
    case GENRE_TRANCE:
      base = 0.1f;
      scale = 0.2f;
      amount = 0.1f;
      break;
    case GENRE_DNB:
      base = 0.2f;
      scale = 0.25f;
      amount = 0.16f;
      break;
    case GENRE_BREAK:
      base = 0.25f;
      scale = 0.3f;
      amount = 0.18f;
      break;
    case GENRE_UKG:
      base = 0.45f;
      scale = 0.35f;
      amount = 0.28f;
      break;
    case GENRE_BOOM:
      base = 0.5f;
      scale = 0.4f;
      amount = 0.32f;
      break;
    case GENRE_DEMBOW:
      base = 0.25f;
      scale = 0.3f;
      amount = 0.18f;
      break;
    case GENRE_FOOT:
      base = 0.15f;
      scale = 0.25f;
      amount = 0.12f;
      break;
    default:
      break;
    }
    return (base + swing_norm_ * scale) * amount;
  }

  void emitStep(uint32_t step)
  {
    // Corner Fill is a dedicated one-bar snare roll (not just Y energy).
    if (fill_timer_ > 0U)
    {
      triggerCornerFill(step);
      --fill_timer_;
      return;
    }
    triggerStep(step);
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

  float velocityJitter(float base, float spread)
  {
    return fx::clip01(base * ((1.f - spread) + fx::randomFloat(rng_) * (spread * 2.f)));
  }

  void fire(HitKind kind, float velocity, float kick_hz0, float kick_hz1, float snare_hz0, float snare_hz1,
            float jitter)
  {
    const float vel = velocityJitter(velocity, jitter);
    switch (kind)
    {
    case kKick:
      kick_age_ = 0.f;
      kick_vel_ = vel;
      kick_hz_ = kick_hz0 + tone_norm_ * kick_hz1;
      kick_phase_ = 0.f;
      ++main_triggers_;
      break;
    case kSnare:
      snare_age_ = 0.f;
      snare_vel_ = vel;
      snare_hz_ = snare_hz0 + tone_norm_ * snare_hz1;
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

  bool fillActive() const { return fill_norm_ > 0.9f; }

  // Top-right one-bar Fill: clear accelerating snare roll with kick anchors.
  void triggerCornerFill(uint32_t step)
  {
    float kick_hz0 = 50.f;
    float kick_hz1 = 30.f;
    float snare_hz0 = 180.f;
    float snare_hz1 = 80.f;
    float jitter = 0.16f;
    switch (activeKitIndex())
    {
    case GENRE_DNB:
      kick_hz0 = 48.f;
      kick_hz1 = 32.f;
      snare_hz0 = 170.f;
      snare_hz1 = 90.f;
      jitter = 0.17f;
      break;
    case GENRE_BREAK:
      kick_hz0 = 50.f;
      kick_hz1 = 35.f;
      snare_hz0 = 175.f;
      snare_hz1 = 95.f;
      jitter = 0.19f;
      break;
    case GENRE_UKG:
      kick_hz0 = 48.f;
      kick_hz1 = 36.f;
      snare_hz0 = 165.f;
      snare_hz1 = 90.f;
      jitter = 0.18f;
      break;
    case GENRE_BOOM:
      kick_hz0 = 42.f;
      kick_hz1 = 28.f;
      snare_hz0 = 155.f;
      snare_hz1 = 80.f;
      jitter = 0.2f;
      break;
    case GENRE_DEMBOW:
      kick_hz0 = 46.f;
      kick_hz1 = 30.f;
      snare_hz0 = 190.f;
      snare_hz1 = 70.f;
      jitter = 0.15f;
      break;
    case GENRE_FOOT:
      kick_hz0 = 55.f;
      kick_hz1 = 40.f;
      snare_hz0 = 200.f;
      snare_hz1 = 100.f;
      jitter = 0.19f;
      break;
    case GENRE_TRANCE:
    default:
      break;
    }

    const auto kick = [&](float velocity) {
      fire(kKick, velocity, kick_hz0, kick_hz1, snare_hz0, snare_hz1, jitter);
    };
    const auto snare = [&](float velocity) {
      fire(kSnare, velocity, kick_hz0, kick_hz1, snare_hz0, snare_hz1, jitter);
    };
    const auto hatC = [&](float velocity) {
      fire(kHatClosed, velocity, kick_hz0, kick_hz1, snare_hz0, snare_hz1, jitter);
    };
    const auto hatO = [&](float velocity) {
      fire(kHatOpen, velocity, kick_hz0, kick_hz1, snare_hz0, snare_hz1, jitter);
    };

    // Keep downbeat kicks so the bar still feels anchored.
    if ((step % 4U) == 0U)
      kick(1.f);
    else if ((step % 2U) == 0U && fx::randomFloat(rng_) < 0.3f)
      kick(0.45f);

    // Snare on every 16th — rising velocity through the bar (= audible roll).
    const float roll = static_cast<float>(step) / static_cast<float>(kSteps - 1U);
    snare(fx::clip01(0.42f + roll * 0.5f + fx::randomFloat(rng_) * 0.12f));

    hatC(((step % 2U) == 0U) ? 0.55f : 0.32f);
    if ((step % 4U) == 2U)
      hatO(0.4f + roll * 0.25f);
  }

  void triggerTrance(uint32_t step)
  {
    const bool fill_active = fillActive();
    const float dens = dens_norm_;
    const float build = fill_norm_;
    const auto kick = [&](float v) { fire(kKick, v, 50.f, 28.f, 185.f, 70.f, 0.12f); };
    const auto snare = [&](float v) { fire(kSnare, v, 50.f, 28.f, 185.f, 70.f, 0.12f); };
    const auto ghost = [&](float v) { fire(kGhost, v, 50.f, 28.f, 185.f, 70.f, 0.12f); };
    const auto hatC = [&](float v) { fire(kHatClosed, v, 50.f, 28.f, 185.f, 70.f, 0.12f); };
    const auto hatO = [&](float v) { fire(kHatOpen, v, 50.f, 28.f, 185.f, 70.f, 0.12f); };

    if ((step % 4U) == 0U)
      kick(fill_active ? 1.f : 0.95f);
    else if (fill_active && ((step % 2U) == 0U) && fx::randomFloat(rng_) < 0.4f)
      kick(0.55f);

    if (step == 4U || step == 12U)
      snare(fill_active ? 1.f : 0.9f);
    else if (step == 3U || step == 7U || step == 11U || step == 13U || step == 14U || step == 15U)
    {
      float chance = build * 0.55f;
      if (step >= 12U)
        chance += build * 0.35f;
      if (fill_active)
        chance = 0.95f;
      if (fx::randomFloat(rng_) < chance)
        snare(0.35f + build * 0.45f);
    }

    if ((step % 4U) != 0U && step != 4U && step != 12U && dens > 0.55f && (step % 2U) != 0U)
    {
      if (fx::randomFloat(rng_) < (dens - 0.45f) * 0.6f)
        ghost(0.15f + dens * 0.12f);
    }

    if ((step % 2U) == 0U || step == 1U || step == 5U || step == 9U || step == 13U)
    {
      float hat_chance = 0.25f + dens * 0.7f;
      if ((step % 2U) != 0U)
        hat_chance *= 0.5f + dens * 0.5f;
      if (fill_active)
        hat_chance = 0.95f;
      if (fx::randomFloat(rng_) < hat_chance)
        hatC(((step % 4U) == 0U) ? 0.55f : 0.32f);
    }

    if (step == 2U || step == 6U || step == 10U || step == 14U)
    {
      float open_chance = 0.55f + dens * 0.45f;
      if (fill_active)
        open_chance = 0.95f;
      if (fx::randomFloat(rng_) < open_chance)
        hatO(0.55f + dens * 0.35f);
    }
  }

  void triggerDnb(uint32_t step)
  {
    const bool fill_active = fillActive();
    const float dens = dens_norm_;
    const float brk = fill_norm_;
    const auto kick = [&](float v) { fire(kKick, v, 48.f, 32.f, 170.f, 90.f, 0.17f); };
    const auto snare = [&](float v) { fire(kSnare, v, 48.f, 32.f, 170.f, 90.f, 0.17f); };
    const auto ghost = [&](float v) { fire(kGhost, v, 48.f, 32.f, 170.f, 90.f, 0.17f); };
    const auto hatC = [&](float v) { fire(kHatClosed, v, 48.f, 32.f, 170.f, 90.f, 0.17f); };
    const auto hatO = [&](float v) { fire(kHatOpen, v, 48.f, 32.f, 170.f, 90.f, 0.17f); };

    if (step == 0U || step == 6U || step == 10U)
      kick(fill_active ? 1.f : (step == 0U ? 1.f : 0.75f));
    else if (step == 2U || step == 3U || step == 12U || step == 14U)
    {
      float chance = brk * 0.45f;
      if (fill_active)
        chance = fx::clip01(chance + 0.55f);
      if (fx::randomFloat(rng_) < chance)
        kick(0.4f + brk * 0.35f);
    }

    if (step == 8U)
      snare(fill_active ? 1.f : 0.95f);
    else if (step == 4U || step == 7U || step == 11U || step == 15U)
    {
      float chance = 0.1f + brk * 0.55f;
      if (fill_active)
        chance = 0.9f;
      if (fx::randomFloat(rng_) < chance)
        snare(0.35f + brk * 0.4f);
    }
    else if (fill_active && fx::randomFloat(rng_) < 0.75f)
      snare(0.45f + fx::randomFloat(rng_) * 0.35f);

    if (step != 0U && step != 6U && step != 8U && step != 10U && brk > 0.25f &&
        (step == 1U || step == 5U || step == 9U || step == 13U))
    {
      if (fx::randomFloat(rng_) < brk * 0.35f)
        ghost(0.18f + brk * 0.15f);
    }

    {
      float hat_chance = 0.35f + dens * 0.6f;
      if ((step % 2U) != 0U)
        hat_chance *= 0.55f + dens * 0.45f;
      if (fill_active)
        hat_chance = 0.98f;
      if (fx::randomFloat(rng_) < hat_chance)
        hatC(((step % 4U) == 0U) ? 0.7f : 0.35f + dens * 0.25f);
    }

    if (step == 2U || step == 6U || step == 10U || step == 14U)
    {
      float open_chance = 0.08f + dens * 0.45f + brk * 0.1f;
      if (fill_active)
        open_chance = 0.7f;
      if (fx::randomFloat(rng_) < open_chance)
        hatO(0.45f + dens * 0.3f);
    }
  }

  void triggerBreak(uint32_t step)
  {
    const bool fill_active = fillActive();
    const float dens = dens_norm_;
    const float energy = fill_norm_;
    const auto kick = [&](float v) { fire(kKick, v, 50.f, 35.f, 175.f, 95.f, 0.19f); };
    const auto snare = [&](float v) { fire(kSnare, v, 50.f, 35.f, 175.f, 95.f, 0.19f); };
    const auto ghost = [&](float v) { fire(kGhost, v, 50.f, 35.f, 175.f, 95.f, 0.19f); };
    const auto hatC = [&](float v) { fire(kHatClosed, v, 50.f, 35.f, 175.f, 95.f, 0.19f); };
    const auto hatO = [&](float v) { fire(kHatOpen, v, 50.f, 35.f, 175.f, 95.f, 0.19f); };

    if (step == 0U || step == 2U || step == 6U || step == 10U)
      kick(fill_active ? 1.f : (step == 0U ? 1.f : 0.78f));
    else if (fill_active && ((step % 2U) == 0U) && fx::randomFloat(rng_) < 0.5f)
      kick(0.62f);
    else if (energy > 0.6f && (step == 8U || step == 14U) && fx::randomFloat(rng_) < (energy - 0.45f))
      kick(0.5f);

    if (step == 4U || step == 12U)
      snare(fill_active ? 1.f : 0.94f);
    else if (step == 7U || step == 13U || step == 15U)
    {
      float snare_chance = 0.35f + dens * 0.55f + energy * 0.15f;
      if (step == 7U)
        snare_chance += 0.25f;
      if (fill_active)
        snare_chance = 0.95f;
      if (fx::randomFloat(rng_) < snare_chance)
        snare(step == 7U ? 0.7f : 0.4f + dens * 0.35f);
    }
    else if (fill_active && fx::randomFloat(rng_) < 0.85f)
      snare(0.5f + fx::randomFloat(rng_) * 0.35f);

    if (step != 0U && step != 2U && step != 4U && step != 6U && step != 10U && step != 12U &&
        (step == 1U || step == 3U || step == 5U || step == 8U || step == 9U || step == 11U || step == 14U))
    {
      float ghost_chance = dens * 0.55f;
      if (step == 3U || step == 11U || step == 14U)
        ghost_chance += dens * 0.3f;
      if (fill_active)
        ghost_chance = fx::clip01(ghost_chance + 0.4f);
      if (fx::randomFloat(rng_) < ghost_chance)
        ghost(fx::clip01(0.18f + dens * 0.22f));
    }

    if ((step % 2U) == 0U || step == 1U || step == 5U || step == 9U || step == 11U)
    {
      float hat_chance = 0.45f + energy * 0.5f;
      if ((step % 2U) != 0U)
        hat_chance *= 0.7f + dens * 0.3f;
      if (fill_active)
        hat_chance = 0.96f;
      if (fx::randomFloat(rng_) < hat_chance)
        hatC(((step % 4U) == 0U) ? 0.65f : 0.38f);
    }

    if (step == 6U || step == 14U)
    {
      float open_chance = 0.12f + energy * 0.45f;
      if (fill_active)
        open_chance = 0.7f;
      if (fx::randomFloat(rng_) < open_chance)
        hatO(0.45f + energy * 0.3f);
    }
  }

  void triggerUkg(uint32_t step)
  {
    const bool fill_active = fill_norm_ > 0.88f;
    const float dens = dens_norm_;
    const float fill = fill_norm_;
    const auto kick = [&](float v) { fire(kKick, v, 48.f, 36.f, 165.f, 90.f, 0.18f); };
    const auto snare = [&](float v) { fire(kSnare, v, 48.f, 36.f, 165.f, 90.f, 0.18f); };
    const auto ghost = [&](float v) { fire(kGhost, v, 48.f, 36.f, 165.f, 90.f, 0.18f); };
    const auto hatC = [&](float v) { fire(kHatClosed, v, 48.f, 36.f, 165.f, 90.f, 0.18f); };
    const auto hatO = [&](float v) { fire(kHatOpen, v, 48.f, 36.f, 165.f, 90.f, 0.18f); };

    if (step == 0U || step == 10U)
      kick(fill_active ? 1.f : 0.95f);
    else if (fill_active && ((step % 2U) == 0U) && fx::randomFloat(rng_) < 0.55f)
      kick(0.72f);
    else if (fill > 0.55f && (step == 2U || step == 8U || step == 14U) &&
             fx::randomFloat(rng_) < (fill - 0.45f))
      kick(0.55f);

    if (step == 4U || step == 12U)
      snare(fill_active ? 1.f : 0.92f);
    else if (fill_active)
    {
      if (fx::randomFloat(rng_) < 0.85f)
        snare(0.55f + fx::randomFloat(rng_) * 0.35f);
    }
    else if (fill > 0.7f && (step == 5U || step == 13U) && fx::randomFloat(rng_) < fill * 0.5f)
      snare(0.45f);

    if (step != 0U && step != 4U && step != 10U && step != 12U &&
        (step == 1U || step == 3U || step == 6U || step == 7U || step == 9U || step == 11U ||
         step == 14U || step == 15U))
    {
      float ghost_chance = dens * 0.55f;
      if (step == 3U || step == 11U)
        ghost_chance += dens * 0.35f;
      if (step == 1U || step == 9U)
        ghost_chance += dens * 0.15f;
      ghost_chance += fill * 0.12f;
      if (fill_active)
        ghost_chance = fx::clip01(ghost_chance + 0.45f);
      if (fx::randomFloat(rng_) < ghost_chance)
        ghost(fx::clip01(0.18f + dens * 0.18f + fill * 0.08f));
    }

    if (step == 8U && dens > 0.35f && !fill_active && fx::randomFloat(rng_) < dens * 0.4f)
      kick(0.22f + dens * 0.12f);

    if ((step % 2U) == 0U || step == 1U || step == 5U || step == 9U || step == 13U)
    {
      float hat_chance = 0.35f + fill * 0.55f + dens * 0.15f;
      if ((step % 2U) != 0U)
        hat_chance *= 0.65f + dens * 0.35f;
      if (fill_active)
        hat_chance = 0.95f;
      if (fx::randomFloat(rng_) < hat_chance)
        hatC(((step % 4U) == 0U) ? 0.7f : 0.4f + fill * 0.2f);
    }

    if (step == 2U || step == 6U || step == 10U || step == 14U)
    {
      float open_chance = 0.08f + fill * 0.45f;
      if (fill_active)
        open_chance = 0.7f;
      if (fx::randomFloat(rng_) < open_chance)
        hatO(0.45f + fill * 0.3f);
    }
  }

  void triggerBoom(uint32_t step)
  {
    const bool fill_active = fillActive();
    const float dens = dens_norm_;
    const float hats = fill_norm_;
    const auto kick = [&](float v) { fire(kKick, v, 42.f, 28.f, 155.f, 80.f, 0.2f); };
    const auto snare = [&](float v) { fire(kSnare, v, 42.f, 28.f, 155.f, 80.f, 0.2f); };
    const auto ghost = [&](float v) { fire(kGhost, v, 42.f, 28.f, 155.f, 80.f, 0.2f); };
    const auto hatC = [&](float v) { fire(kHatClosed, v, 42.f, 28.f, 155.f, 80.f, 0.2f); };
    const auto hatO = [&](float v) { fire(kHatOpen, v, 42.f, 28.f, 155.f, 80.f, 0.2f); };

    if (step == 0U || step == 7U || step == 10U)
      kick(fill_active ? 1.f : (step == 0U ? 1.f : 0.82f));
    else if (fill_active && (step == 2U || step == 8U || step == 14U) && fx::randomFloat(rng_) < 0.5f)
      kick(0.55f);

    if (step == 4U || step == 12U)
      snare(fill_active ? 1.f : 0.95f);
    else if (fill_active && fx::randomFloat(rng_) < 0.8f)
      snare(0.5f + fx::randomFloat(rng_) * 0.35f);
    else if (hats > 0.72f && (step == 5U || step == 13U) && fx::randomFloat(rng_) < hats * 0.45f)
      snare(0.4f);

    if (step != 0U && step != 4U && step != 7U && step != 10U && step != 12U &&
        (step == 2U || step == 3U || step == 5U || step == 6U || step == 9U || step == 11U ||
         step == 13U || step == 14U || step == 15U))
    {
      float ghost_chance = dens * 0.5f;
      if (step == 3U || step == 11U)
        ghost_chance += dens * 0.4f;
      if (step == 5U || step == 13U)
        ghost_chance += dens * 0.25f;
      if (fill_active)
        ghost_chance = fx::clip01(ghost_chance + 0.4f);
      if (fx::randomFloat(rng_) < ghost_chance)
        ghost(fx::clip01(0.16f + dens * 0.2f + hats * 0.06f));
    }

    if ((step % 2U) == 0U || step == 3U || step == 7U || step == 11U || step == 15U)
    {
      float hat_chance = 0.4f + hats * 0.55f;
      if ((step % 2U) != 0U)
        hat_chance *= 0.55f + dens * 0.35f;
      if (fill_active)
        hat_chance = 0.95f;
      if (fx::randomFloat(rng_) < hat_chance)
        hatC(((step % 4U) == 0U) ? 0.62f : 0.35f);
    }

    if (step == 6U || step == 14U)
    {
      float open_chance = 0.05f + hats * 0.4f;
      if (fill_active)
        open_chance = 0.65f;
      if (fx::randomFloat(rng_) < open_chance)
        hatO(0.4f + hats * 0.25f);
    }
  }

  void triggerDembow(uint32_t step)
  {
    const bool fill_active = fillActive();
    const float dens = dens_norm_;
    const float perc = fill_norm_;
    const auto kick = [&](float v) { fire(kKick, v, 46.f, 30.f, 190.f, 70.f, 0.15f); };
    const auto snare = [&](float v) { fire(kSnare, v, 46.f, 30.f, 190.f, 70.f, 0.15f); };
    const auto ghost = [&](float v) { fire(kGhost, v, 46.f, 30.f, 190.f, 70.f, 0.15f); };
    const auto hatC = [&](float v) { fire(kHatClosed, v, 46.f, 30.f, 190.f, 70.f, 0.15f); };
    const auto hatO = [&](float v) { fire(kHatOpen, v, 46.f, 30.f, 190.f, 70.f, 0.15f); };

    if (step == 0U || step == 6U || step == 8U || step == 14U)
      kick(fill_active ? 1.f : (step == 0U || step == 8U ? 0.98f : 0.78f));
    else if (fill_active && ((step % 2U) == 0U) && fx::randomFloat(rng_) < 0.45f)
      kick(0.6f);

    if (step == 4U || step == 12U)
      snare(fill_active ? 1.f : 0.92f);
    else if (step == 7U || step == 15U)
      snare(fill_active ? 0.85f : 0.7f + dens * 0.2f);
    else if (fill_active && fx::randomFloat(rng_) < 0.75f)
      snare(0.45f + fx::randomFloat(rng_) * 0.35f);

    if (step == 2U || step == 3U || step == 5U || step == 9U || step == 11U || step == 13U)
    {
      float rim_chance = 0.15f + dens * 0.7f;
      if (step == 3U || step == 11U)
        rim_chance += dens * 0.2f;
      if (fill_active)
        rim_chance = fx::clip01(rim_chance + 0.35f);
      if (fx::randomFloat(rng_) < rim_chance)
        ghost(fx::clip01(0.22f + dens * 0.25f));
    }

    if (step == 1U || step == 3U || step == 5U || step == 9U || step == 11U || step == 13U)
    {
      float hat_chance = 0.2f + perc * 0.65f + dens * 0.1f;
      if (fill_active)
        hat_chance = 0.92f;
      if (fx::randomFloat(rng_) < hat_chance)
        hatC(0.35f + perc * 0.25f);
    }

    if (step == 2U || step == 10U)
    {
      float open_chance = 0.1f + perc * 0.5f;
      if (fill_active)
        open_chance = 0.7f;
      if (fx::randomFloat(rng_) < open_chance)
        hatO(0.5f + perc * 0.25f);
    }
  }

  void triggerFoot(uint32_t step)
  {
    const bool fill_active = fillActive();
    const float dens = dens_norm_;
    const float roll = fill_norm_;
    const auto kick = [&](float v) { fire(kKick, v, 55.f, 40.f, 200.f, 100.f, 0.19f); };
    const auto snare = [&](float v) { fire(kSnare, v, 55.f, 40.f, 200.f, 100.f, 0.19f); };
    const auto hatC = [&](float v) { fire(kHatClosed, v, 55.f, 40.f, 200.f, 100.f, 0.19f); };
    const auto hatO = [&](float v) { fire(kHatOpen, v, 55.f, 40.f, 200.f, 100.f, 0.19f); };

    if (step == 0U || step == 3U || step == 8U || step == 11U)
      kick(fill_active ? 1.f : 0.95f);
    else if (step == 1U || step == 2U || step == 5U || step == 6U || step == 9U || step == 10U ||
             step == 13U || step == 14U)
    {
      float stut_chance = dens * 0.75f;
      if ((step % 4U) == 1U || (step % 4U) == 2U)
        stut_chance += dens * 0.2f;
      if (fill_active)
        stut_chance = fx::clip01(stut_chance + 0.55f);
      if (fx::randomFloat(rng_) < stut_chance)
      {
        kick(0.45f + dens * 0.4f);
        ++ghost_triggers_;
      }
    }

    if (step == 4U || step == 12U)
      snare(fill_active ? 1.f : 0.9f);
    else if (step == 5U || step == 6U || step == 7U || step == 13U || step == 14U || step == 15U)
    {
      float roll_chance = roll * 0.55f;
      if (step == 6U || step == 7U || step == 14U || step == 15U)
        roll_chance += roll * 0.35f;
      if (fill_active)
        roll_chance = 0.95f;
      if (fx::randomFloat(rng_) < roll_chance)
        snare(0.35f + roll * 0.45f + fx::randomFloat(rng_) * 0.2f);
    }

    {
      float hat_chance = 0.55f + roll * 0.4f + dens * 0.1f;
      if (fill_active)
        hat_chance = 0.98f;
      if (fx::randomFloat(rng_) < hat_chance)
        hatC(((step % 2U) == 0U) ? 0.55f : 0.32f);
    }

    if ((step == 7U || step == 15U) && fx::randomFloat(rng_) < (0.15f + roll * 0.5f))
      hatO(0.45f + roll * 0.3f);
  }

  void triggerStep(uint32_t step)
  {
    switch (genre_)
    {
    case GENRE_TRANCE:
      triggerTrance(step);
      break;
    case GENRE_DNB:
      triggerDnb(step);
      break;
    case GENRE_BREAK:
      triggerBreak(step);
      break;
    case GENRE_UKG:
      triggerUkg(step);
      break;
    case GENRE_BOOM:
      triggerBoom(step);
      break;
    case GENRE_DEMBOW:
      triggerDembow(step);
      break;
    case GENRE_FOOT:
      triggerFoot(step);
      break;
    default:
      triggerTrance(step);
      break;
    }
  }

  int32_t activeKitIndex() const
  {
    if (kit_ <= KIT_AUTO || kit_ >= NUM_KITS)
      return genre_;
    return kit_ - 1; // KIT_TRANCE..KIT_FOOT → GENRE_TRANCE..GENRE_FOOT
  }

  VoiceFeel voiceFeel() const
  {
    // Compact synthetic kits (no PCM) — pick via KIT, not locked to GENRE.
    static const VoiceFeel kFeels[NUM_GENRES] = {
        // TRANCE — clappy snare, long open hats
        {0.05f, 0.09f, 0.04f, 0.07f, 0.018f, 0.025f, 0.012f, 0.02f, 0.09f, 0.14f, 36.f, 0.0028f, 1.4f,
         0.18f, 0.82f, 0.95f, 0.3f, 0.f, 1.7f, 0.38f, 0.28f, 0.62f},
        // DNB
        {0.04f, 0.08f, 0.035f, 0.07f, 0.016f, 0.025f, 0.01f, 0.016f, 0.05f, 0.08f, 38.f, 0.0025f, 1.35f,
         0.28f, 0.72f, 1.05f, 0.38f, 0.f, 1.7f, 0.42f, 0.34f, 0.5f},
        // BREAK
        {0.035f, 0.07f, 0.03f, 0.06f, 0.016f, 0.025f, 0.011f, 0.018f, 0.055f, 0.09f, 40.f, 0.0028f, 1.3f,
         0.3f, 0.7f, 1.0f, 0.4f, 0.2f, 1.8f, 0.38f, 0.3f, 0.48f},
        // UKG
        {0.045f, 0.09f, 0.035f, 0.07f, 0.018f, 0.025f, 0.012f, 0.02f, 0.06f, 0.1f, 38.f, 0.0022f, 1.35f,
         0.32f, 0.68f, 0.95f, 0.42f, 0.18f, 1.7f, 0.35f, 0.32f, 0.48f},
        // BOOM
        {0.055f, 0.11f, 0.04f, 0.08f, 0.02f, 0.03f, 0.014f, 0.022f, 0.07f, 0.1f, 34.f, 0.0016f, 1.4f,
         0.28f, 0.72f, 0.98f, 0.38f, 0.16f, 1.55f, 0.32f, 0.28f, 0.42f},
        // DEMBOW — brighter cha + rim tone
        {0.05f, 0.1f, 0.03f, 0.06f, 0.016f, 0.02f, 0.012f, 0.018f, 0.08f, 0.12f, 36.f, 0.002f, 1.38f,
         0.22f, 0.78f, 1.05f, 0.2f, 0.55f, 2.4f, 0.4f, 0.3f, 0.55f},
        // FOOT — short punchy
        {0.028f, 0.05f, 0.022f, 0.04f, 0.014f, 0.02f, 0.01f, 0.015f, 0.045f, 0.07f, 42.f, 0.0035f, 1.25f,
         0.2f, 0.8f, 1.05f, 0.35f, 0.f, 1.7f, 0.45f, 0.34f, 0.5f},
    };
    const int32_t kit_index = activeKitIndex();
    const int32_t safe_index =
        (kit_index < 0) ? 0 : ((kit_index >= NUM_GENRES) ? (NUM_GENRES - 1) : kit_index);
    return kFeels[safe_index];
  }

  float renderVoices()
  {
    const VoiceFeel feel = voiceFeel();
    const float sr = getSampleRate();
    const float kick_tau = (feel.kick_tau0 + decay_norm_ * feel.kick_tau1) * sr;
    const float snare_tau = (feel.snare_tau0 + decay_norm_ * feel.snare_tau1) * sr;
    const float ghost_tau = (feel.ghost_tau0 + decay_norm_ * feel.ghost_tau1) * sr;
    const float hat_c_tau = (feel.hat_c_tau0 + decay_norm_ * feel.hat_c_tau1) * sr;
    const float hat_o_tau = (feel.hat_o_tau0 + decay_norm_ * feel.hat_o_tau1) * sr;

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

    kick_hz_ += (feel.kick_target_hz - kick_hz_) * feel.kick_glide;
    kick_phase_ = fx::wrap01(kick_phase_ + kick_hz_ / sr);
    const float kick = fastersinfullf(kick_phase_ * kTwoPi) * kick_env * feel.kick_gain;

    snare_phase_ = fx::wrap01(snare_phase_ + snare_hz_ / sr);
    const float snare_tone = fastersinfullf(snare_phase_ * kTwoPi);
    const float noise = fx::randomFloat(rng_) * 2.f - 1.f;
    const float snare =
        (snare_tone * feel.snare_tone + noise * feel.snare_noise) * snare_env * feel.snare_gain;

    const float ghost =
        noise * ghost_env * feel.ghost_noise +
        fastersinfullf(fx::wrap01(snare_phase_ * feel.ghost_tone_ratio) * kTwoPi) * ghost_env *
            feel.ghost_tone;

    hat_hp_ += feel.hat_hp_coeff * (noise - hat_hp_);
    const float hat_bright = noise - hat_hp_;
    const float hats = hat_bright * (hat_c_env * feel.hat_c_gain + hat_o_env * feel.hat_o_gain);

    return fx::softclip(kick + snare + ghost + hats);
  }

  int32_t genre_ = GENRE_TRANCE;
  int32_t kit_ = KIT_AUTO;
  float bpm_ = 138.f;
  float dens_norm_ = 0.44f;
  float fill_norm_ = 0.27f;
  float swing_norm_ = 0.34f;
  float tone_norm_ = 0.45f;
  float decay_norm_ = 0.44f;
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
  uint32_t rng_ = 0xD4A55u;
  uint32_t ghost_triggers_ = 0U;
  uint32_t main_triggers_ = 0U;
  int32_t swing_samples_left_ = 0;
  bool running_ = false;
  bool use_host_clock_ = false;
  bool fill_corner_latched_ = false;
};
