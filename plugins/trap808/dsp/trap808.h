#pragma once

/*
 * File: trap808.h
 *
 * Trap phrase pad for NTS-3: closed/open hats with rolls, half-time snare,
 * punchy kick, and a sliding sine 808. Hold to run; top-right flick fills.
 *
 * Envelopes are age-based (HClap-style). Avoid fasterexpf for near-1
 * per-sample multiply coeffs — that approximation is biased around 0.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class Trap808 : public Processor
{
public:
  static constexpr uint32_t kStepsPerBar = 16U;
  static constexpr uint32_t kHatVoices = 8U;
  static constexpr uint32_t kPercVoices = 4U;
  static constexpr float kDcCoeff = 0.99608f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    HATS = 0U,
    TUNE,
    MIX,
    GROOVE,
    DECAY,
    DRIVE,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case HATS:
      hats_norm_ = param_10bit_to_f32(value);
      break;
    case TUNE:
      tune_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case GROOVE:
      groove_norm_ = param_10bit_to_f32(value);
      break;
    case DECAY:
      decay_norm_ = param_10bit_to_f32(value);
      break;
    case DRIVE:
      drive_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *) override final
  {
    bpm_ = 140.f;
    running_ = false;
    fill_timer_ = 0U;
    clock_acc_ = 0.f;
    step_index_ = 0U;
    rng_ = 0xC0FFEEu;
    noise_state_ = 0xA5A5A5A5u;
    noise_lp_ = 0.f;
    dc_prev_in_ = 0.f;
    dc_prev_out_ = 0.f;
    bass_phase_ = 0.f;
    bass_age_ = 10.f;
    bass_amp_ = 0.f;
    bass_midi_ = 33.f;
    bass_target_midi_ = 33.f;
    bass_active_ = false;
    hat_roll_remaining_ = 0;
    hat_roll_countdown_ = 0;
    hat_roll_interval_ = 0;
    hat_roll_velocity_ = 0.f;
    hat_roll_delta_ = 0.f;
    resetVoices();
  }

  void reset() override final
  {
    running_ = false;
    fill_timer_ = 0U;
    hat_roll_remaining_ = 0;
    bass_active_ = false;
    resetVoices();
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 40.f && tempo < 300.f)
      bpm_ = tempo;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      running_ = false;
      return;
    }
    if (phase == k_unit_touch_phase_began)
    {
      running_ = true;
      step_index_ = 0U;
      clock_acc_ = 0.f;
      if (x > 760U && y > 760U)
        fill_timer_ = kStepsPerBar;
      triggerStep(0U);
      step_index_ = 1U;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    (void)raw;
    const float sixteenth = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate())) * 0.25f;
    const float inv_sr = 1.f / getSampleRate();
    const float noise_coeff = fx::onePoleCoeff(9000.f, getSampleRate());

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      if (running_)
      {
        clock_acc_ += 1.f;
        if (clock_acc_ >= sixteenth)
        {
          clock_acc_ -= sixteenth;
          triggerStep(step_index_);
          step_index_ = (step_index_ + 1U) % kStepsPerBar;
          if (fill_timer_ > 0U)
            --fill_timer_;
        }
        advanceHatRoll();
      }

      noise_lp_ += noise_coeff * (whiteNoise() - noise_lp_);
      const float bright_noise = whiteNoise() - noise_lp_;

      const float wet = renderMix(bright_noise, inv_sr);
      out[0] = fx::mix(in[0], wet, mix_);
      out[1] = fx::mix(in[1], wet * 0.98f, mix_);
      in += 2;
      out += 2;
    }
  }

  void debugForceRunning() { running_ = true; }

  uint32_t debugHatTriggers() const { return hat_trigger_count_; }

  uint32_t debugKickTriggers() const { return kick_trigger_count_; }

  void debugResetCounters()
  {
    hat_trigger_count_ = 0U;
    kick_trigger_count_ = 0U;
  }

private:
  struct PercVoice
  {
    bool active = false;
    float age = 0.f;
    float accent = 1.f;
    float phase = 0.f;
    float start_hz = 60.f;
    float end_hz = 48.f;
  };

  struct HatVoice
  {
    bool active = false;
    float age = 0.f;
    float accent = 1.f;
    bool open = false;
  };

  void resetVoices()
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kPercVoices; ++voiceIndex)
    {
      kicks_[voiceIndex] = PercVoice{};
      snares_[voiceIndex] = PercVoice{};
    }
    for (uint32_t voiceIndex = 0; voiceIndex < kHatVoices; ++voiceIndex)
      hats_[voiceIndex] = HatVoice{};
    next_kick_ = 0U;
    next_snare_ = 0U;
    next_hat_ = 0U;
  }

  float whiteNoise()
  {
    noise_state_ = noise_state_ * 1664525U + 1013904223U;
    return (static_cast<float>(noise_state_) * (1.f / 2147483648.f)) - 1.f;
  }

  float rootMidi() const
  {
    return 28.f + tune_norm_ * 24.f;
  }

  static float velocityForHatStep(uint32_t step_index)
  {
    // Downbeats loud, offbeat 16ths softer — classic trap hat groove.
    if ((step_index % 4U) == 0U)
      return 1.f;
    if ((step_index % 2U) == 0U)
      return 0.72f;
    return 0.48f;
  }

  bool shouldClosedHat(uint32_t step_index, bool fill)
  {
    if (fill)
      return true;
    if (hats_norm_ < 0.18f)
      return (step_index % 2U) == 0U;
    if (hats_norm_ < 0.42f)
      return (step_index % 2U) == 0U || fx::randomFloat(rng_) < (hats_norm_ * 1.4f);
    return true;
  }

  bool shouldOpenHat(uint32_t step_index, bool fill)
  {
    if (fill)
      return (step_index % 4U) == 3U;
    if (hats_norm_ < 0.35f)
      return false;
    // Offbeat open accents; sparse so rolls stay readable.
    if ((step_index % 8U) == 6U)
      return hats_norm_ > 0.45f;
    if ((step_index % 8U) == 2U)
      return hats_norm_ > 0.7f && fx::randomFloat(rng_) < 0.45f;
    return false;
  }

  bool shouldKick(uint32_t step_index, bool fill)
  {
    if (fill)
      return step_index == 0U || step_index == 8U || ((step_index & 3U) == 0U && groove_norm_ > 0.55f);
    if (step_index == 0U)
      return true;
    if (step_index == 6U)
      return groove_norm_ > 0.25f;
    if (step_index == 10U)
      return groove_norm_ > 0.4f;
    if (step_index == 14U)
      return groove_norm_ > 0.55f;
    if (step_index == 3U || step_index == 11U)
      return groove_norm_ > 0.75f && fx::randomFloat(rng_) < 0.55f;
    return false;
  }

  bool shouldSnare(uint32_t step_index, bool fill)
  {
    // Half-time trap: snare on beat 3 (step 8).
    if (step_index == 8U)
      return true;
    if (fill && (step_index == 12U || step_index == 14U))
      return true;
    if (step_index == 12U)
      return groove_norm_ > 0.65f;
    if (step_index == 4U)
      return groove_norm_ > 0.85f && fx::randomFloat(rng_) < 0.35f;
    return false;
  }

  float bassIntervalSemitones(uint32_t step_index) const
  {
    // Minor-leaning trap 808 intervals relative to root.
    static const float kIntervals[8] = {0.f, -5.f, -7.f, -12.f, 3.f, -2.f, -10.f, 5.f};
    const uint32_t pick = (step_index + static_cast<uint32_t>(groove_norm_ * 5.f)) & 7U;
    return kIntervals[pick];
  }

  void triggerKick(float accent)
  {
    PercVoice &voice = kicks_[next_kick_];
    next_kick_ = (next_kick_ + 1U) % kPercVoices;
    voice.active = true;
    voice.age = 0.f;
    voice.accent = accent;
    voice.phase = 0.f;
    voice.start_hz = 160.f + groove_norm_ * 40.f;
    voice.end_hz = 48.f + tune_norm_ * 12.f;
    ++kick_trigger_count_;
  }

  void triggerSnare(float accent)
  {
    PercVoice &voice = snares_[next_snare_];
    next_snare_ = (next_snare_ + 1U) % kPercVoices;
    voice.active = true;
    voice.age = 0.f;
    voice.accent = accent;
    voice.phase = 0.f;
    voice.start_hz = 220.f;
    voice.end_hz = 180.f;
  }

  void triggerHat(float accent, bool open)
  {
    if (!open)
    {
      // Closed hats choke any ringing open hat.
      for (uint32_t voiceIndex = 0; voiceIndex < kHatVoices; ++voiceIndex)
      {
        if (hats_[voiceIndex].active && hats_[voiceIndex].open)
          hats_[voiceIndex].active = false;
      }
    }
    HatVoice &voice = hats_[next_hat_];
    next_hat_ = (next_hat_ + 1U) % kHatVoices;
    voice.active = true;
    voice.age = 0.f;
    voice.accent = accent;
    voice.open = open;
    ++hat_trigger_count_;
  }

  void triggerBass(float midi_note)
  {
    bass_target_midi_ = midi_note;
    if (!bass_active_ || bass_amp_ < 0.05f)
      bass_midi_ = midi_note;
    bass_age_ = 0.f;
    bass_amp_ = 1.f;
    bass_active_ = true;
  }

  void scheduleHatRoll(uint32_t step_index, float base_velocity, bool fill)
  {
    const float sixteenth = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate())) * 0.25f;
    float subdiv = 1.f;
    uint32_t hits = 0U;

    if (fill)
    {
      hits = 6U + (step_index & 3U);
      subdiv = 4.f;
    }
    else if (hats_norm_ > 0.88f && ((step_index % 4U) == 3U || step_index == 7U || step_index == 15U))
    {
      // Triplet burst into the next beat.
      hits = 3U;
      subdiv = 3.f;
    }
    else if (hats_norm_ > 0.62f && (step_index == 6U || step_index == 7U || step_index == 14U || step_index == 15U))
    {
      hits = 3U + static_cast<uint32_t>((hats_norm_ - 0.62f) * 10.f);
      subdiv = 4.f;
    }
    else if (hats_norm_ > 0.78f && (step_index & 1U) != 0U)
    {
      hits = 2U;
      subdiv = 2.f;
    }

    if (hits < 2U)
      return;

    // First hit already fired by the step; schedule the remaining roll hits.
    hat_roll_remaining_ = static_cast<int32_t>(hits - 1U);
    hat_roll_interval_ = static_cast<int32_t>(sixteenth / subdiv);
    if (hat_roll_interval_ < 24)
      hat_roll_interval_ = 24;
    hat_roll_countdown_ = hat_roll_interval_;
    hat_roll_velocity_ = base_velocity;
    // Front-weighted decay across the roll (avoids machine-gun feel).
    hat_roll_delta_ = -base_velocity * (0.12f + hats_norm_ * 0.08f);
  }

  void advanceHatRoll()
  {
    if (hat_roll_remaining_ <= 0)
      return;
    --hat_roll_countdown_;
    if (hat_roll_countdown_ > 0)
      return;
    triggerHat(fx::clip01(hat_roll_velocity_), false);
    hat_roll_velocity_ += hat_roll_delta_;
    --hat_roll_remaining_;
    hat_roll_countdown_ = hat_roll_interval_;
  }

  void triggerStep(uint32_t step_index)
  {
    const bool fill = fill_timer_ > 0U;

    if (shouldKick(step_index, fill))
    {
      const float accent = (step_index == 0U) ? 1.f : 0.82f;
      triggerKick(accent);
      triggerBass(rootMidi() + bassIntervalSemitones(step_index));
    }
    else if (groove_norm_ > 0.35f && (step_index == 4U || step_index == 12U) &&
             fx::randomFloat(rng_) < (0.25f + groove_norm_ * 0.4f))
    {
      // Occasional 808 slide without a kick.
      triggerBass(rootMidi() + bassIntervalSemitones(step_index + 3U));
    }

    if (shouldSnare(step_index, fill))
      triggerSnare((step_index == 8U) ? 1.f : 0.7f);

    if (shouldOpenHat(step_index, fill))
      triggerHat(0.85f, true);
    else if (shouldClosedHat(step_index, fill))
    {
      const float velocity = velocityForHatStep(step_index);
      triggerHat(velocity, false);
      scheduleHatRoll(step_index, velocity * 0.9f, fill);
    }
  }

  float renderKick(PercVoice &voice, float inv_sr)
  {
    if (!voice.active)
      return 0.f;
    const float pitch_env = fasterexpf(-voice.age / 0.028f);
    const float hz = voice.end_hz + (voice.start_hz - voice.end_hz) * pitch_env;
    const float amp = fasterexpf(-voice.age / 0.12f) * voice.accent;
    const float click = fasterexpf(-voice.age / 0.004f) * voice.accent * 0.35f;
    voice.phase = fx::wrap01(voice.phase + hz * inv_sr);
    const float body = fastersinfullf(voice.phase * 6.283185307179586f);
    voice.age += inv_sr;
    if (voice.age > 0.45f || amp < 0.001f)
      voice.active = false;
    return (body * amp + click) * (0.95f + drive_norm_ * 0.35f);
  }

  float renderSnare(PercVoice &voice, float noise, float inv_sr)
  {
    if (!voice.active)
      return 0.f;
    const float tone_amp = fasterexpf(-voice.age / 0.09f) * voice.accent;
    const float noise_amp = fasterexpf(-voice.age / 0.14f) * voice.accent;
    voice.phase = fx::wrap01(voice.phase + voice.end_hz * inv_sr);
    const float tone = fastersinfullf(voice.phase * 6.283185307179586f);
    voice.age += inv_sr;
    if (voice.age > 0.5f || (tone_amp < 0.001f && noise_amp < 0.001f))
      voice.active = false;
    return tone * tone_amp * 0.35f + noise * noise_amp * 0.75f;
  }

  float renderHat(HatVoice &voice, float noise, float inv_sr)
  {
    if (!voice.active)
      return 0.f;
    const float tau = voice.open ? 0.18f : 0.028f;
    const float amp = fasterexpf(-voice.age / tau) * voice.accent;
    voice.age += inv_sr;
    if (voice.age > tau * 8.f || amp < 0.001f)
      voice.active = false;
    return noise * amp * (voice.open ? 0.55f : 0.42f);
  }

  float renderBass(float inv_sr)
  {
    if (!bass_active_)
      return 0.f;

    // Glide toward target (trap 808 slides).
    bass_midi_ += (bass_target_midi_ - bass_midi_) * (0.0009f + groove_norm_ * 0.0012f);

    const float tau = 0.22f + decay_norm_ * 1.35f;
    const float amp = fasterexpf(-bass_age_ / tau);
    const float pitch_drop = fasterexpf(-bass_age_ / 0.045f);
    const float midi = bass_midi_ + pitch_drop * 18.f;
    const float hz = fx::noteToHz(midi);
    bass_phase_ = fx::wrap01(bass_phase_ + hz * inv_sr);
    const float sine = fastersinfullf(bass_phase_ * 6.283185307179586f);
    // Soft upper harmonic so the sub reads on small speakers.
    const float crunch = fastertanhf(sine * (1.4f + drive_norm_ * 2.2f));
    bass_age_ += inv_sr;
    bass_amp_ = amp;
    if (bass_age_ > tau * 7.f || amp < 0.0015f)
      bass_active_ = false;
    return crunch * amp * (0.85f + drive_norm_ * 0.25f);
  }

  float renderMix(float noise, float inv_sr)
  {
    float sum = 0.f;
    for (uint32_t voiceIndex = 0; voiceIndex < kPercVoices; ++voiceIndex)
    {
      sum += renderKick(kicks_[voiceIndex], inv_sr);
      sum += renderSnare(snares_[voiceIndex], noise, inv_sr) * 0.9f;
    }
    for (uint32_t voiceIndex = 0; voiceIndex < kHatVoices; ++voiceIndex)
      sum += renderHat(hats_[voiceIndex], noise, inv_sr);

    sum += renderBass(inv_sr) * 0.95f;

    const float blocked = sum - dc_prev_in_ + kDcCoeff * dc_prev_out_;
    dc_prev_in_ = sum;
    dc_prev_out_ = blocked;
    // Pre-scale before softclip — fastertanhf is inaccurate for |x| >> 1.
    return fx::softclip(blocked * (0.32f + drive_norm_ * 0.08f));
  }

  PercVoice kicks_[kPercVoices];
  PercVoice snares_[kPercVoices];
  HatVoice hats_[kHatVoices];
  uint32_t next_kick_ = 0U;
  uint32_t next_snare_ = 0U;
  uint32_t next_hat_ = 0U;
  uint32_t step_index_ = 0U;
  uint32_t fill_timer_ = 0U;
  uint32_t hat_trigger_count_ = 0U;
  uint32_t kick_trigger_count_ = 0U;
  uint32_t rng_ = 1U;
  uint32_t noise_state_ = 1U;
  int32_t hat_roll_remaining_ = 0;
  int32_t hat_roll_countdown_ = 0;
  int32_t hat_roll_interval_ = 0;
  float hat_roll_velocity_ = 0.f;
  float hat_roll_delta_ = 0.f;
  float clock_acc_ = 0.f;
  float bpm_ = 140.f;
  float hats_norm_ = 0.55f;
  float tune_norm_ = 0.35f;
  float groove_norm_ = 0.4f;
  float decay_norm_ = 0.55f;
  float drive_norm_ = 0.45f;
  float mix_ = 1.f;
  float noise_lp_ = 0.f;
  float dc_prev_in_ = 0.f;
  float dc_prev_out_ = 0.f;
  float bass_phase_ = 0.f;
  float bass_age_ = 10.f;
  float bass_amp_ = 0.f;
  float bass_midi_ = 33.f;
  float bass_target_midi_ = 33.f;
  bool bass_active_ = false;
  bool running_ = false;
};
