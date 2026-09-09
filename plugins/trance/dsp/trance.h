#pragma once

/*
 * File: trance.h
 *
 * Tempo-synced trance drum kit for NTS-3.
 * Hold to run. X = offbeat 909-hat energy. Y = build / fill.
 *
 * Voices follow the real TR-909 split:
 *   Hats  — 6-bit ROM PCM (HN61256P C43, same dump as Trap808 / HHat)
 *   Kick  — analog model (triangle VCO → soft sine shaper + pitch sweep + click)
 *   Clap  — analog model (LFSR noise, sawtooth burst train + room tail; HClap 909 path)
 *
 * There is no BD/clap ROM to dump on a 909 — only hats/ride/crash are PCM.
 * Top-right flick = one-bar Fill. Hits lock to host 4ppqn.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "trance_hh_pcm.h"
#include "utils/float_math.h"
#include <stdint.h>

class Trance : public Processor
{
public:
  static constexpr uint32_t kSteps = 16U;
  static constexpr uint32_t kHatVoices = 6U;
  static constexpr float kTwoPi = 6.283185307179586f;
  static constexpr float kHhRomPhaseInc = kTranceHhRomClockHz / 48000.f;
  static constexpr float kDacMid = 32.f;
  static constexpr float kDacScale = 1.f / 32.f;

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
    next_hat_ = 0U;
    rng_ = 0x7A7CE1u;
    noise_state_ = 0xA5A5A5A5u;
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
    kick_vel_ = 0.f;
    clap_vel_ = 0.f;
    kick_phase_ = 0.f;
    kick_hz_ = 55.f;
    kick_end_hz_ = 48.f;
    clap_crack_low_ = 0.f;
    clap_crack_band_ = 0.f;
    clap_room_low_ = 0.f;
    clap_room_band_ = 0.f;
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

  float velocityJitter(float base)
  {
    return fx::clip01(base * (0.88f + fx::randomFloat(rng_) * 0.24f));
  }

  float whiteNoise()
  {
    noise_state_ = noise_state_ * 1664525U + 1013904223U;
    return (static_cast<float>(noise_state_) * (1.f / 2147483648.f)) - 1.f;
  }

  // 31-stage LFSR (Electric Druid / TR-909 digital noise), taps 31 and 13.
  float lfsrNoise()
  {
    const uint32_t bit = ((lfsr_ >> 30) ^ (lfsr_ >> 12)) & 1U;
    lfsr_ = ((lfsr_ << 1) | bit) & 0x7FFFFFFFu;
    if (lfsr_ == 0U)
      lfsr_ = 0x7FFFFFFFu;
    return (lfsr_ & 1U) ? 1.f : -1.f;
  }

  // HClap-style sawtooth burst train (service-note clap chatter).
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

  static uint8_t readPacked6(const uint8_t *packed, uint32_t sample_index)
  {
    const uint32_t bit_index = sample_index * 6U;
    const uint32_t byte_index = bit_index >> 3;
    const uint32_t shift = bit_index & 7U;
    const uint32_t pair = static_cast<uint32_t>(packed[byte_index]) |
                          (static_cast<uint32_t>(packed[byte_index + 1U]) << 8);
    return static_cast<uint8_t>((pair >> shift) & 0x3FU);
  }

  void fireKick(float velocity)
  {
    kick_age_ = 0.f;
    kick_vel_ = velocityJitter(velocity);
    // 909 BD: pitch sweep from ~150–200 Hz down into the body.
    kick_hz_ = 145.f + tone_norm_ * 70.f;
    kick_end_hz_ = 42.f + tone_norm_ * 18.f;
    kick_phase_ = 0.f;
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
      // Closed chokes open, like the 909 shared ROM path.
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
    // Near-1 multiply coeff: linearize (do not use fasterexpf here).
    const float tau = open ? (0.1f + decay_norm_ * 0.22f) : (0.028f + decay_norm_ * 0.02f);
    const float x = -1.f / (tau * getSampleRate());
    voice.env_coeff = 1.f + x;
    ++ghost_triggers_;
  }

  void triggerStep(uint32_t step)
  {
    const bool fill_active = fill_timer_ > 0U || build_norm_ > 0.9f;
    const float hats = hats_norm_;
    const float build = build_norm_;

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
      float hat_chance = 0.25f + hats * 0.7f;
      if ((step % 2U) != 0U)
        hat_chance *= 0.5f + hats * 0.5f;
      if (fill_active)
        hat_chance = 0.95f;
      if (fx::randomFloat(rng_) < hat_chance)
        fireHat(((step % 4U) == 0U) ? 0.6f : 0.35f, false);
    }

    if (isOpenHatSeat(step))
    {
      float open_chance = 0.55f + hats * 0.45f;
      if (fill_active)
        open_chance = 0.95f;
      if (fx::randomFloat(rng_) < open_chance)
        fireHat(0.55f + hats * 0.35f, true);
    }
  }

  float renderHat(HatVoice &voice)
  {
    if (!voice.active)
      return 0.f;

    const uint32_t length = voice.open ? kTranceHhOpenLength : kTranceHhClosedLength;
    const uint8_t *packed = voice.open ? kTranceHhOpenPacked : kTranceHhClosedPacked;
    const uint32_t sample_index = static_cast<uint32_t>(voice.rom_phase);
    if (sample_index >= length)
    {
      voice.active = false;
      return 0.f;
    }

    const float code = static_cast<float>(readPacked6(packed, sample_index));
    const float raw = (code - kDacMid) * kDacScale;
    voice.lpf += 0.55f * (raw - voice.lpf);
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

    // Triangle → soft sine shaper (909 diode waveshaper flavor).
    const float centered = kick_phase_ - 0.5f;
    const float abs_centered = (centered < 0.f) ? -centered : centered;
    const float tri = 4.f * abs_centered - 1.f;
    const float body = fastertanhf(tri * 1.35f);

    // Short noise click / "Tone" transient.
    const float click = fasterexpf(-kick_age_ / 0.0045f) * whiteNoise() * kick_vel_ * 0.22f;
    kick_age_ += inv_sr;
    return (body * amp * 1.25f + click);
  }

  float renderClap(float inv_sr)
  {
    // HClap 909 path: LFSR → dual BP → burst VCA + room VCA.
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

  float renderVoices()
  {
    const float inv_sr = 1.f / getSampleRate();
    const float kick = renderKick(inv_sr);
    const float clap = renderClap(inv_sr);

    float hats = 0.f;
    for (uint32_t voiceIndex = 0; voiceIndex < kHatVoices; ++voiceIndex)
      hats += renderHat(hats_[voiceIndex]);

    return fx::softclip(kick + clap + hats);
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
  float clap_age_ = 1e9f;
  float kick_vel_ = 0.f;
  float clap_vel_ = 0.f;
  float kick_phase_ = 0.f;
  float kick_hz_ = 55.f;
  float kick_end_hz_ = 48.f;
  float clap_crack_low_ = 0.f;
  float clap_crack_band_ = 0.f;
  float clap_room_low_ = 0.f;
  float clap_room_band_ = 0.f;

  HatVoice hats_[kHatVoices];
  uint32_t next_hat_ = 0U;
  uint32_t tick_counter_ = 0U;
  uint32_t pending_step_ = 0U;
  uint32_t fill_timer_ = 0U;
  uint32_t rng_ = 0x7A7CE1u;
  uint32_t noise_state_ = 0xA5A5A5A5u;
  uint32_t lfsr_ = 0x7FFFFFFFu;
  uint32_t ghost_triggers_ = 0U;
  uint32_t main_triggers_ = 0U;
  int32_t swing_samples_left_ = 0;
  bool running_ = false;
  bool use_host_clock_ = false;
};
