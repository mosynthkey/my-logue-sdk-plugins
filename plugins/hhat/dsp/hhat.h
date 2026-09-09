#pragma once

/*
 * File: hhat.h
 *
 * Tempo-synced modeled hi-hat for NTS-3.
 * Hold the pad to run a 16-step phrase. X is Euclidean hit density
 * (step-synced). Y is a continuous Close → Open continuum — not two
 * discrete machine keys. See RESEARCH.md.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class HHat : public Processor
{
public:
  static constexpr uint32_t kVoiceCount = 8U;
  static constexpr uint32_t kStepsPerBar = 16U;
  static constexpr uint32_t kMetalCount = 6U;
  static constexpr float kVoiceGain = 0.42f;
  static constexpr float kDcCoeff = 0.99608f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    DENS = 0U,
    OPEN,
    MIX,
    TONE,
    TUNE,
    DEC,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case DENS:
      dens_norm_ = param_10bit_to_f32(value);
      break;
    case OPEN:
      open_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case TONE:
      tone_norm_ = param_10bit_to_f32(value);
      break;
    case TUNE:
      tune_norm_ = param_10bit_to_f32(value);
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
    dens_norm_ = 0.47f;
    open_norm_ = 0.f;
    tone_norm_ = 0.55f;
    tune_norm_ = 0.5f;
    decay_norm_ = 0.5f;
    mix_ = 1.f;
    bpm_ = 120.f;
    running_ = false;
    use_host_clock_ = false;
    tick_counter_ = 0U;
    internal_tick_phase_ = 0.f;
    roll_samples_left_ = 0;
    roll_hits_left_ = 0;
    analog_state_ = 0xA5A5C3C3u;
    dc_prev_in_ = 0.f;
    dc_prev_out_ = 0.f;
    trigger_count_ = 0U;
    bp_low_ = 0.f;
    bp_band_ = 0.f;
    for (uint32_t oscIndex = 0; oscIndex < kMetalCount; ++oscIndex)
      metal_phase_[oscIndex] = static_cast<float>(oscIndex) * 0.13f;
    resetVoices();
  }

  void reset() override final
  {
    running_ = false;
    roll_samples_left_ = 0;
    roll_hits_left_ = 0;
    dc_prev_in_ = 0.f;
    dc_prev_out_ = 0.f;
    bp_low_ = 0.f;
    bp_band_ = 0.f;
    resetVoices();
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 20.f && tempo < 999.f)
      bpm_ = tempo;
  }

  void tempo4ppqnTick(uint32_t counter) override final
  {
    use_host_clock_ = true;
    handleTick(counter);
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
        phase == k_unit_touch_phase_stationary)
    {
      running_ = true;
      return;
    }

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      running_ = false;
      roll_samples_left_ = 0;
      roll_hits_left_ = 0;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    const float dry_gain = 1.f - mix_;
    const float inv_sr = 1.f / getSampleRate();
    const float tune = fasterpow2f((tune_norm_ * 2.f - 1.f) * 0.55f);
    const float tone = tone_norm_;
    const float bp_hz = 5200.f + tone * 3800.f;
    const float bp_f = svfF(bp_hz);
    const float bp_damp = 0.55f + (1.f - tone) * 0.35f;
    const float decay_scale = 0.55f + decay_norm_ * 1.1f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      if (!use_host_clock_)
        advanceInternalClockOneSample();
      advancePendingRoll();

      const float metal = renderMetal(tune, inv_sr);
      const float band = bandpass(metal, bp_f, bp_damp, bp_low_, bp_band_);
      const float noise = analogNoise();
      const float wet = renderVoices(band, noise, decay_scale, inv_sr) * mix_;
      out[0] = in[0] * dry_gain + wet;
      out[1] = in[1] * dry_gain + wet;
      in += 2;
      out += 2;
    }
  }

  void debugTrigger(float accent = 1.f) { triggerVoice(accent, open_norm_); }

  void debugSetOpen(float open_norm) { open_norm_ = fx::clip01(open_norm); }

  uint32_t debugTriggerCount() const { return trigger_count_; }

  uint32_t debugHits() const { return hitsFromDensity(dens_norm_); }

  bool debugStepHit(uint32_t step_index) const
  {
    return stepIsHit(step_index, hitsFromDensity(dens_norm_));
  }

  float debugTauSeconds() const { return tauFromOpen(open_norm_, 0.55f + decay_norm_ * 1.1f); }

private:
  struct Voice
  {
    bool active = false;
    float age = 0.f;
    float accent = 1.f;
    float open_amount = 0.f;
    float tau = 0.05f;
    float hp_z = 0.f;
  };

  static uint32_t hitsFromDensity(float dens_norm)
  {
    uint32_t hits = 1U + static_cast<uint32_t>(dens_norm * 15.f + 0.5f);
    if (hits < 1U)
      hits = 1U;
    if (hits > 16U)
      hits = 16U;
    return hits;
  }

  // Hats sit on the grid from step 0 (unlike snare/clap 2-and-4 rotate).
  static bool stepIsHit(uint32_t step_index, uint32_t hits)
  {
    return fx::euclidHit(step_index % kStepsPerBar, hits, kStepsPerBar);
  }

  static bool isDownbeatSixteenth(uint32_t step_index)
  {
    return (step_index % 4U) == 0U;
  }

  static float tauFromOpen(float open_norm, float decay_scale)
  {
    const float closed_tau = 0.045f;
    const float open_tau = 0.32f;
    return (closed_tau + open_norm * (open_tau - closed_tau)) * decay_scale;
  }

  static float svfF(float hz)
  {
    const float clamped = fx::clip(hz, 800.f, 12000.f);
    return 2.f * fastersinfullf(3.14159265f * clamped / 48000.f);
  }

  static float bandpass(float input, float f, float damp, float &low, float &band)
  {
    low += f * band;
    const float high = input - low - damp * band;
    band += f * high;
    return band;
  }

  static float squareFromPhase(float phase)
  {
    return (phase < 0.5f) ? 1.f : -1.f;
  }

  float analogNoise()
  {
    analog_state_ = analog_state_ * 1664525U + 1013904223U;
    return (static_cast<float>(analog_state_) * (1.f / 2147483648.f)) - 1.f;
  }

  float renderMetal(float tune, float inv_sr)
  {
    // Werner / Baratatronix factory-ish 808 metal bank (Hz).
    static const float kMetalHz[kMetalCount] = {205.3f, 304.4f, 369.6f, 522.7f, 540.f, 800.f};
    float sum = 0.f;
    for (uint32_t oscIndex = 0; oscIndex < kMetalCount; ++oscIndex)
    {
      const float hz = kMetalHz[oscIndex] * tune;
      metal_phase_[oscIndex] = fx::wrap01(metal_phase_[oscIndex] + hz * inv_sr);
      sum += squareFromPhase(metal_phase_[oscIndex]);
    }
    return sum * (1.f / static_cast<float>(kMetalCount));
  }

  void resetVoices()
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
      voices_[voiceIndex] = Voice{};
    next_voice_index_ = 0U;
  }

  void handleTick(uint32_t counter)
  {
    tick_counter_ = counter;
    if (!running_)
      return;

    const uint32_t step_index = (counter - 1U) % kStepsPerBar;
    const uint32_t hits = hitsFromDensity(dens_norm_);
    if (!stepIsHit(step_index, hits))
      return;

    const float accent = isDownbeatSixteenth(step_index) ? 1.f : 0.82f;
    triggerVoice(accent, open_norm_);

    // Dense end of X: 32nd rolls on even sixteenths (machine-gun hats).
    if (dens_norm_ > 0.84f && bpm_ > 0.f && (step_index % 2U) == 0U)
    {
      const float samples_per_16th = getSampleRate() * 60.f / (bpm_ * 4.f);
      roll_samples_left_ = static_cast<int32_t>(samples_per_16th * 0.5f);
      roll_hits_left_ = 1 + static_cast<int32_t>((dens_norm_ - 0.84f) * 8.f);
    }
  }

  void advanceInternalClockOneSample()
  {
    if (bpm_ <= 0.f)
      return;

    const float samples_per_tick = getSampleRate() * 60.f / (bpm_ * 4.f);
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

  void advancePendingRoll()
  {
    if (roll_hits_left_ <= 0 || roll_samples_left_ <= 0)
      return;
    --roll_samples_left_;
    if (roll_samples_left_ > 0)
      return;

    triggerVoice(0.62f, open_norm_ * 0.35f);
    --roll_hits_left_;
    if (roll_hits_left_ > 0 && bpm_ > 0.f)
    {
      const float samples_per_16th = getSampleRate() * 60.f / (bpm_ * 4.f);
      roll_samples_left_ = static_cast<int32_t>(samples_per_16th * 0.5f);
    }
  }

  void chokeOpenVoices(float new_open)
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
    {
      Voice &voice = voices_[voiceIndex];
      if (!voice.active)
        continue;
      // More-closed hit (or roll) chokes ringing open / half-open voices.
      if (voice.open_amount > new_open + 0.08f)
      {
        voice.tau *= 0.22f;
        if (voice.tau < 0.012f)
          voice.tau = 0.012f;
        if (voice.age < voice.tau * 0.35f)
          voice.age = voice.tau * 0.35f;
      }
    }
  }

  void triggerVoice(float accent, float open_amount)
  {
    const float open = fx::clip01(open_amount);
    chokeOpenVoices(open);

    Voice &voice = voices_[next_voice_index_];
    next_voice_index_ = (next_voice_index_ + 1U) % kVoiceCount;
    voice.active = true;
    voice.age = 0.f;
    voice.accent = accent;
    voice.open_amount = open;
    voice.tau = tauFromOpen(open, 0.55f + decay_norm_ * 1.1f);
    voice.hp_z = 0.f;
    ++trigger_count_;
  }

  float renderVoices(float metal_band, float noise, float decay_scale, float inv_sr)
  {
    (void)decay_scale;
    float sum = 0.f;
    const float pedal = open_norm_;

    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
    {
      Voice &voice = voices_[voiceIndex];
      if (!voice.active)
        continue;

      // Foot choke: closing the pad while a note rings shortens it.
      if (pedal + 0.12f < voice.open_amount)
      {
        voice.tau *= 0.997f;
        if (voice.tau < 0.012f)
          voice.tau = 0.012f;
      }

      const float amp = fasterexpf(-voice.age / voice.tau) * voice.accent;
      const float tick = fasterexpf(-voice.age / 0.008f) * voice.accent;

      const float hp_hz = 9200.f - voice.open_amount * 4800.f;
      const float hp_coeff = fx::onePoleCoeff(hp_hz, getSampleRate());
      voice.hp_z += hp_coeff * (metal_band - voice.hp_z);
      const float bright = metal_band - voice.hp_z;

      const float shimmer = (noise - voice.hp_z * 0.15f) * (0.08f + voice.open_amount * 0.12f);
      const float body = bright * amp + shimmer * amp + noise * tick * 0.18f;

      voice.age += inv_sr;
      if (voice.age > voice.tau * 8.f || amp < 0.001f)
        voice.active = false;

      sum += body * kVoiceGain;
    }

    const float blocked = sum - dc_prev_in_ + kDcCoeff * dc_prev_out_;
    dc_prev_in_ = sum;
    dc_prev_out_ = blocked;
    return fx::softclip(blocked);
  }

  Voice voices_[kVoiceCount];
  float metal_phase_[kMetalCount];
  uint32_t next_voice_index_ = 0U;
  uint32_t tick_counter_ = 0U;
  uint32_t analog_state_ = 1U;
  uint32_t trigger_count_ = 0U;
  int32_t roll_samples_left_ = 0;
  int32_t roll_hits_left_ = 0;
  float dens_norm_ = 0.f;
  float open_norm_ = 0.f;
  float tone_norm_ = 0.55f;
  float tune_norm_ = 0.5f;
  float decay_norm_ = 0.5f;
  float mix_ = 1.f;
  float bpm_ = 120.f;
  float internal_tick_phase_ = 0.f;
  float bp_low_ = 0.f;
  float bp_band_ = 0.f;
  float dc_prev_in_ = 0.f;
  float dc_prev_out_ = 0.f;
  bool running_ = false;
  bool use_host_clock_ = false;
};
