#pragma once

/*
 * File: trance2.h
 *
 * Tempo-synced trance drums + rolling bassline for NTS-3.
 * Hold to run. X = bass complexity. Y = drum complexity / build.
 *
 * Bass templates (uplifting trance convention):
 *   low  X — offbeat 1/8 between kicks
 *   mid  X — rolling 16ths on the root (e & a of each beat)
 *   high X — same roll + walk-ups / leading tones near bar end
 *
 * Drums follow Trance (909 ROM hats + analog kick/clap models).
 * Top-right flick = one-bar Fill. Hits lock to host 4ppqn.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "tr909_hh_crop_pcm.h"
#include "tr909_pcm.h"
#include "utils/float_math.h"
#include <stdint.h>

class Trance2 : public Processor
{
public:
  static constexpr uint32_t kSteps = 16U;
  static constexpr uint32_t kHatVoices = 6U;
  static constexpr float kTwoPi = 6.283185307179586f;
  static constexpr float kHhRomPhaseInc = tr909::kRomPhaseInc;
  static constexpr float kRootMidi = 33.f; // A1 — common trance sub root

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    BASS = 0U,
    DRUM,
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
    case BASS:
      bass_norm_ = param_10bit_to_f32(value);
      break;
    case DRUM:
      drum_norm_ = param_10bit_to_f32(value);
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
    next_hat_ = 0U;
    rng_ = 0xC0FFEE11u;
    noise_state_ = 0xA5A5A5A5u;
    duck_gain_ = 1.f;
    resetVoices();
  }

  void reset() override final
  {
    running_ = false;
    fill_timer_ = 0U;
    swing_samples_left_ = 0;
    duck_gain_ = 1.f;
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
  uint32_t debugBassTriggers() const { return bass_triggers_; }
  uint32_t debugTickCounter() const { return tick_counter_; }
  void debugResetCounters()
  {
    ghost_triggers_ = 0U;
    main_triggers_ = 0U;
    bass_triggers_ = 0U;
  }
  void debugForceRun() { running_ = true; }
  void debugTriggerStep(uint32_t step) { triggerStep(step); }

private:
  struct HatVoice
  {
    bool active = false;
    bool open = false;
    float accent = 1.f;
    float rom_phase = 0.f;
    float env = 0.f;
    float env_coeff = 0.f;
    float lpf = 0.f;
  };

  void resetVoices()
  {
    kick_age_ = 1e9f;
    clap_age_ = 1e9f;
    bass_age_ = 1e9f;
    kick_vel_ = 0.f;
    clap_vel_ = 0.f;
    bass_vel_ = 0.f;
    kick_phase_ = 0.f;
    kick_hz_ = 55.f;
    kick_end_hz_ = 48.f;
    clap_crack_low_ = 0.f;
    clap_crack_band_ = 0.f;
    clap_room_low_ = 0.f;
    clap_room_band_ = 0.f;
    bass_midi_ = kRootMidi;
    bass_saw_phase_ = 0.f;
    bass_sub_phase_ = 0.f;
    bass_lp_ = 0.f;
    lfsr_ = 0x7FFFFFFFu;
    for (uint32_t voiceIndex = 0; voiceIndex < kHatVoices; ++voiceIndex)
      hats_[voiceIndex] = HatVoice{};
    next_hat_ = 0U;
  }

  float samplesPerSixteenth() const
  {
    if (bpm_ <= 0.f)
      return 0.f;
    return getSampleRate() * 60.f / (bpm_ * 4.f);
  }

  float swingDelayFraction() const
  {
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

  static bool isKickSpine(uint32_t step) { return (step % 4U) == 0U; }
  static bool isClapSpine(uint32_t step) { return step == 4U || step == 12U; }
  static bool isClosedHatSeat(uint32_t step)
  {
    return (step % 2U) == 0U || step == 1U || step == 5U || step == 9U || step == 13U;
  }
  static bool isOpenHatSeat(uint32_t step)
  {
    return step == 2U || step == 6U || step == 10U || step == 14U;
  }
  static bool isBuildClapSeat(uint32_t step)
  {
    return step == 3U || step == 7U || step == 11U || step == 13U || step == 14U || step == 15U;
  }
  static bool isOffbeatEighth(uint32_t step) { return (step % 4U) == 2U; }
  static bool isRollingSixteenth(uint32_t step)
  {
    const uint32_t within = step % 4U;
    return within == 1U || within == 2U || within == 3U;
  }

  float velocityJitter(float base)
  {
    return fx::clip01(base * (0.88f + fx::randomFloat(rng_) * 0.24f));
  }

  float whiteNoise()
  {
    noise_state_ = noise_state_ * 1664525U + 1013904223U;
    return (static_cast<float>(noise_state_) * (1.f / 2147483648.f)) - 1.f;
  }

  float lfsrNoise()
  {
    const uint32_t bit = ((lfsr_ >> 30) ^ (lfsr_ >> 12)) & 1U;
    lfsr_ = ((lfsr_ << 1) | bit) & 0x7FFFFFFFu;
    if (lfsr_ == 0U)
      lfsr_ = 0x7FFFFFFFu;
    return (lfsr_ & 1U) ? 1.f : -1.f;
  }

  static float clapBurstEnv(float age_sec, float spacing)
  {
    const float last_span = spacing * 2.f;
    const float first_span = spacing * 3.f;
    if (age_sec < 0.f)
      return 0.f;
    if (age_sec < first_span)
    {
      const float phase = age_sec / spacing;
      const float within = phase - static_cast<float>(static_cast<int32_t>(phase));
      return 1.f - within;
    }
    const float last_age = age_sec - first_span;
    if (last_age < last_span)
      return 1.f - last_age / last_span;
    return 0.f;
  }

  static float svfF(float hz)
  {
    const float clamped = fx::clip(hz, 120.f, 8000.f);
    return 2.f * fastersinfullf(3.14159265f * clamped / 48000.f);
  }

  static float bandpass(float input, float f, float damp, float &low, float &band)
  {
    low += f * band;
    const float high = input - low - damp * band;
    band += f * high;
    return band;
  }

  void fireKick(float velocity)
  {
    kick_age_ = 0.f;
    kick_vel_ = velocityJitter(velocity);
    kick_hz_ = 145.f + tone_norm_ * 70.f;
    kick_end_hz_ = 42.f + tone_norm_ * 18.f;
    kick_phase_ = 0.f;
    // Instant duck under kick; recover over ~120 ms at 138 BPM.
    duck_gain_ = 0.08f;
    ++main_triggers_;
  }

  void fireClap(float velocity)
  {
    clap_age_ = 0.f;
    clap_vel_ = velocityJitter(velocity);
    clap_crack_low_ = 0.f;
    clap_crack_band_ = 0.f;
    clap_room_low_ = 0.f;
    clap_room_band_ = 0.f;
    ++main_triggers_;
  }

  void fireHat(float velocity, bool open)
  {
    if (!open)
    {
      for (uint32_t voiceIndex = 0; voiceIndex < kHatVoices; ++voiceIndex)
      {
        if (hats_[voiceIndex].active && hats_[voiceIndex].open)
          hats_[voiceIndex].active = false;
      }
    }

    HatVoice &voice = hats_[next_hat_];
    next_hat_ = (next_hat_ + 1U) % kHatVoices;
    voice.active = true;
    voice.open = open;
    voice.accent = velocityJitter(velocity);
    voice.rom_phase = 0.f;
    voice.env = 1.f;
    voice.lpf = 0.f;
    const float tau = open ? (0.1f + decay_norm_ * 0.22f) : (0.028f + decay_norm_ * 0.02f);
    const float x = -1.f / (tau * getSampleRate());
    voice.env_coeff = 1.f + x;
    ++ghost_triggers_;
  }

  float bassIntervalSemitones(uint32_t step) const
  {
    // High-X walks: keep the roll on root, colour the bar ending.
    if (bass_norm_ < 0.55f)
      return 0.f;

    const float walk = (bass_norm_ - 0.55f) * (1.f / 0.45f);
    if (step == 15U)
      return (walk > 0.55f) ? 7.f : ((walk > 0.25f) ? 5.f : -1.f);
    if (step == 14U && walk > 0.7f)
      return 3.f;
    if (step == 11U && walk > 0.85f)
      return -5.f;
    // Occasional octave sparkle on early "a" seats at max complexity.
    if ((step == 3U || step == 7U) && walk > 0.92f)
      return 12.f;
    return 0.f;
  }

  void fireBass(uint32_t step, float velocity)
  {
    bass_age_ = 0.f;
    bass_vel_ = velocityJitter(velocity);
    bass_midi_ = kRootMidi + bassIntervalSemitones(step);
    ++bass_triggers_;
  }

  void triggerBassForStep(uint32_t step)
  {
    const float bass = bass_norm_;
    if (bass < 0.02f)
      return;
    if (isKickSpine(step))
      return;

    // Progressive offbeat 1/8 always present once X is up a little.
    if (isOffbeatEighth(step))
    {
      float chance = 0.55f + bass * 0.45f;
      float velocity = 0.72f;
      // Soften the "&" slightly for breath (tutorial tip).
      velocity *= 0.9f;
      if (fx::randomFloat(rng_) < chance)
        fireBass(step, velocity);
      return;
    }

    // Rolling 16ths (e / a) open with mid X.
    if (!isRollingSixteenth(step))
      return;

    float roll_amount = (bass - 0.28f) * (1.f / 0.72f);
    if (roll_amount <= 0.f)
      return;
    roll_amount = fx::clip01(roll_amount);

    float chance = 0.2f + roll_amount * 0.8f;
    if ((step % 4U) == 1U)
      chance *= 0.85f + roll_amount * 0.15f;
    if (bass > 0.85f && step == 13U)
      chance *= 0.7f; // drop one pickup for groove at max complexity

    float velocity = 0.55f + roll_amount * 0.35f;
    if ((step % 4U) == 2U)
      velocity *= 0.88f;

    if (fx::randomFloat(rng_) < chance)
      fireBass(step, velocity);
  }

  void triggerStep(uint32_t step)
  {
    const bool fill_active = fill_timer_ > 0U || drum_norm_ > 0.9f;
    const float drum = drum_norm_;
    // Y morphs hats + build: low = spine only, high = dense hats + clap rolls.
    const float hats = fx::clip01(drum * 1.05f);
    const float build = fx::clip01((drum - 0.28f) * (1.f / 0.72f));

    if (isKickSpine(step))
      fireKick(fill_active ? 1.f : 0.95f);
    else if (fill_active && ((step % 2U) == 0U) && fx::randomFloat(rng_) < 0.4f)
      fireKick(0.55f);

    if (isClapSpine(step))
      fireClap(fill_active ? 1.f : 0.9f);
    else if (isBuildClapSeat(step))
    {
      float chance = build * 0.55f;
      if (step >= 12U)
        chance += build * 0.35f;
      if (fill_active)
        chance = 0.95f;
      if (fx::randomFloat(rng_) < chance)
        fireClap(0.35f + build * 0.45f);
    }

    if (isClosedHatSeat(step))
    {
      float hat_chance = 0.18f + hats * 0.75f;
      if ((step % 2U) != 0U)
        hat_chance *= 0.45f + hats * 0.55f;
      if (drum < 0.12f)
        hat_chance *= 0.25f;
      if (fill_active)
        hat_chance = 0.95f;
      if (fx::randomFloat(rng_) < hat_chance)
        fireHat(((step % 4U) == 0U) ? 0.6f : 0.35f, false);
    }

    if (isOpenHatSeat(step))
    {
      float open_chance = 0.35f + hats * 0.6f;
      if (drum < 0.15f)
        open_chance *= 0.35f;
      if (fill_active)
        open_chance = 0.95f;
      if (fx::randomFloat(rng_) < open_chance)
        fireHat(0.55f + hats * 0.35f, true);
    }

    triggerBassForStep(step);
  }

  float renderHat(HatVoice &voice)
  {
    if (!voice.active)
      return 0.f;

    const uint32_t length = voice.open ? kTr909HhCropOpenLength : kTr909HhCropClosedLength;
    const uint8_t *packed = voice.open ? kTr909HhCropOpenPacked : kTr909HhCropClosedPacked;
    const uint32_t sample_index = static_cast<uint32_t>(voice.rom_phase);
    if (sample_index >= length)
    {
      voice.active = false;
      return 0.f;
    }

    const float raw = tr909::dacFromPacked(packed, sample_index);
    voice.lpf += tr909::kSimpleLpfCoeff * (raw - voice.lpf);
    const float sample = voice.lpf * voice.env * voice.accent * (voice.open ? 0.78f : 0.52f);
    voice.rom_phase += kHhRomPhaseInc;
    voice.env *= voice.env_coeff;
    if (voice.env < 0.001f || voice.rom_phase >= static_cast<float>(length))
      voice.active = false;
    return sample;
  }

  float renderKick(float inv_sr)
  {
    const float amp_tau = 0.085f + decay_norm_ * 0.16f;
    const float pitch_tau = 0.028f + tone_norm_ * 0.02f;
    const float amp = (kick_age_ < amp_tau * 8.f) ? fasterexpf(-kick_age_ / amp_tau) * kick_vel_ : 0.f;
    const float pitch_env = fasterexpf(-kick_age_ / pitch_tau);
    const float start_hz = 145.f + tone_norm_ * 70.f;
    const float hz = kick_end_hz_ + (start_hz - kick_end_hz_) * pitch_env;
    kick_phase_ = fx::wrap01(kick_phase_ + hz * inv_sr);

    const float centered = kick_phase_ - 0.5f;
    const float abs_centered = (centered < 0.f) ? -centered : centered;
    const float tri = 4.f * abs_centered - 1.f;
    const float body = fastertanhf(tri * 1.35f);

    const float click = fasterexpf(-kick_age_ / 0.0045f) * whiteNoise() * kick_vel_ * 0.22f;
    kick_age_ += inv_sr;
    return (body * amp * 1.25f + click);
  }

  float renderClap(float inv_sr)
  {
    const float spacing = 0.012f;
    const float tail_tau = 0.11f + decay_norm_ * 0.12f;
    const float tone_ratio = fasterpow2f((tone_norm_ * 2.f - 1.f) * 0.4f);
    const float crack_f = svfF(1400.f * tone_ratio);
    const float room_f = svfF(950.f * (0.9f + tone_norm_ * 0.15f));
    const float crack_damp = 1.f / 1.55f;
    const float room_damp = 1.f / 0.95f;

    const float noise = lfsrNoise();
    const float burst = clapBurstEnv(clap_age_, spacing) * clap_vel_;
    const float tail = fasterexpf(-clap_age_ / tail_tau) * clap_vel_;
    const float crack = bandpass(noise, crack_f, crack_damp, clap_crack_low_, clap_crack_band_);
    const float room = bandpass(noise, room_f, room_damp, clap_room_low_, clap_room_band_);
    const float sample = crack * burst * 0.95f + room * tail * 0.42f;

    clap_age_ += inv_sr;
    if (clap_age_ > spacing * 5.f + tail_tau * 6.f)
      clap_vel_ = 0.f;
    return sample * 0.85f;
  }

  float renderBass(float inv_sr)
  {
    // Age-based pluck (~35–70 ms) — clears before the next kick / 16th.
    const float amp_tau = 0.032f + decay_norm_ * 0.04f;
    const float amp = (bass_vel_ > 0.001f && bass_age_ < amp_tau * 8.f)
                          ? fasterexpf(-bass_age_ / amp_tau) * bass_vel_
                          : 0.f;

    const float mid_inc = fx::noteToInc(bass_midi_, getSampleRate());
    const float sub_inc = fx::noteToInc(bass_midi_ - 12.f, getSampleRate());
    bass_saw_phase_ = fx::wrap01(bass_saw_phase_ + mid_inc);
    bass_sub_phase_ = fx::wrap01(bass_sub_phase_ + sub_inc);

    const float saw = fx::blepSaw(bass_saw_phase_, mid_inc);
    const float sub = fastersinfullf(bass_sub_phase_ * kTwoPi);

    // Filter envelope: subtle downward sweep on each note.
    const float filter_env = fasterexpf(-bass_age_ / (amp_tau * 1.15f));
    const float cutoff = 180.f + tone_norm_ * 900.f + filter_env * (600.f + tone_norm_ * 1400.f);
    const float coeff = fx::onePoleCoeff(cutoff, getSampleRate());
    bass_lp_ += coeff * (saw - bass_lp_);

    const float mid = bass_lp_ * 0.55f;
    const float layered = mid * 0.72f + sub * 0.55f;
    const float driven = fastertanhf(layered * (1.15f + tone_norm_ * 0.55f));

    // Sidechain recover toward 1 over ~120 ms.
    const float duck_coeff = 1.f / (0.12f * getSampleRate());
    duck_gain_ += (1.f - duck_gain_) * duck_coeff;
    if (duck_gain_ > 1.f)
      duck_gain_ = 1.f;

    bass_age_ += inv_sr;
    if (amp < 0.001f)
      bass_vel_ = 0.f;

    return driven * amp * duck_gain_ * 0.85f;
  }

  float renderVoices()
  {
    const float inv_sr = 1.f / getSampleRate();
    const float kick = renderKick(inv_sr);
    const float clap = renderClap(inv_sr);
    const float bass = renderBass(inv_sr);

    float hats = 0.f;
    for (uint32_t voiceIndex = 0; voiceIndex < kHatVoices; ++voiceIndex)
      hats += renderHat(hats_[voiceIndex]);

    return fx::softclip(kick + clap + hats + bass);
  }

  float bpm_ = 138.f;
  float bass_norm_ = 0.55f;
  float drum_norm_ = 0.35f;
  float swing_norm_ = 0.2f;
  float tone_norm_ = 0.45f;
  float decay_norm_ = 0.45f;
  float mix_ = 1.f;
  float internal_tick_phase_ = 0.f;

  float kick_age_ = 1e9f;
  float clap_age_ = 1e9f;
  float bass_age_ = 1e9f;
  float kick_vel_ = 0.f;
  float clap_vel_ = 0.f;
  float bass_vel_ = 0.f;
  float kick_phase_ = 0.f;
  float kick_hz_ = 55.f;
  float kick_end_hz_ = 48.f;
  float clap_crack_low_ = 0.f;
  float clap_crack_band_ = 0.f;
  float clap_room_low_ = 0.f;
  float clap_room_band_ = 0.f;
  float bass_midi_ = kRootMidi;
  float bass_saw_phase_ = 0.f;
  float bass_sub_phase_ = 0.f;
  float bass_lp_ = 0.f;
  float duck_gain_ = 1.f;

  HatVoice hats_[kHatVoices];
  uint32_t next_hat_ = 0U;
  uint32_t tick_counter_ = 0U;
  uint32_t pending_step_ = 0U;
  uint32_t fill_timer_ = 0U;
  uint32_t rng_ = 0xC0FFEE11u;
  uint32_t noise_state_ = 0xA5A5A5A5u;
  uint32_t lfsr_ = 0x7FFFFFFFu;
  uint32_t ghost_triggers_ = 0U;
  uint32_t main_triggers_ = 0U;
  uint32_t bass_triggers_ = 0U;
  int32_t swing_samples_left_ = 0;
  bool running_ = false;
  bool use_host_clock_ = false;
};
