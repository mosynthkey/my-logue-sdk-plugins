#pragma once

/*
 * File: hclap.h
 *
 * Tempo-synced 808/909 hand clap for NTS-3.
 * Hold the pad to run a 16-step phrase. X is hit density, Y morphs the
 * circuit from TR-808 (analog noise, one VCA) to TR-909 (LFSR, dual VCA).
 *
 * Voice path follows the service-note clap, not a sample. See RESEARCH.md.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class HClap : public Processor
{
public:
  static constexpr uint32_t kVoiceCount = 6U;
  static constexpr uint32_t kStepsPerBar = 16U;
  static constexpr float kVoiceGain = 0.38f;
  static constexpr float kDcCoeff = 0.99608f;
  static constexpr float kAnalogColorHz = 9000.f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    DENS = 0U,
    TYPE,
    MIX,
    TONE,
    DEC,
    SNAP,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case DENS:
      dens_norm_ = param_10bit_to_f32(value);
      break;
    case TYPE:
      type_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case TONE:
      tone_norm_ = param_10bit_to_f32(value);
      break;
    case DEC:
      decay_norm_ = param_10bit_to_f32(value);
      break;
    case SNAP:
      snap_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *) override final
  {
    dens_norm_ = 0.068f;
    type_norm_ = 0.f;
    tone_norm_ = 0.5f;
    decay_norm_ = 0.5f;
    snap_norm_ = 0.5f;
    mix_ = 1.f;
    bpm_ = 120.f;
    running_ = false;
    use_host_clock_ = false;
    tick_counter_ = 0U;
    internal_tick_phase_ = 0.f;
    flam_samples_left_ = 0;
    analog_state_ = 0x00C0FFEEu;
    analog_lp_ = 0.f;
    lfsr_ = 0x7FFFFFFFu;
    dc_prev_in_ = 0.f;
    dc_prev_out_ = 0.f;
    trigger_count_ = 0U;
    analog_color_coeff_ = fx::onePoleCoeff(kAnalogColorHz, getSampleRate());
    resetVoices();
  }

  void reset() override final
  {
    running_ = false;
    flam_samples_left_ = 0;
    dc_prev_in_ = 0.f;
    dc_prev_out_ = 0.f;
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
      flam_samples_left_ = 0;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    const float dry_gain = 1.f - mix_;
    const float analog_coeff = analog_color_coeff_;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      if (!use_host_clock_)
        advanceInternalClockOneSample();
      advancePendingFlam();

      analog_lp_ += analog_coeff * (analogNoise() - analog_lp_);
      const float digital = lfsrNoise();
      const float noise = analog_lp_ + type_norm_ * (digital - analog_lp_);

      const float wet = renderVoices(noise) * mix_;
      out[0] = in[0] * dry_gain + wet;
      out[1] = in[1] * dry_gain + wet;
      in += 2;
      out += 2;
    }
  }

  void debugTrigger(float accent = 1.f) { triggerVoice(accent); }

  uint32_t debugTriggerCount() const { return trigger_count_; }

  uint32_t debugHits() const { return hitsFromDensity(dens_norm_); }

  bool debugStepHit(uint32_t step_index) const
  {
    return stepIsHit(step_index, hitsFromDensity(dens_norm_));
  }

private:
  struct Voice
  {
    bool active = false;
    float age = 0.f;
    float accent = 1.f;
    float crack_low = 0.f;
    float crack_band = 0.f;
    float room_low = 0.f;
    float room_band = 0.f;
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

  static bool stepIsHit(uint32_t step_index, uint32_t hits)
  {
    return fx::euclidHit((step_index + 12U) % kStepsPerBar, hits, kStepsPerBar);
  }

  static bool isBackbeat(uint32_t step_index)
  {
    return step_index == 4U || step_index == 12U;
  }

  static float burstEnv(float age, float spacing)
  {
    const float last_span = spacing * 2.f;
    const float first_span = spacing * 3.f;
    if (age < 0.f)
      return 0.f;
    if (age < first_span)
    {
      const float phase = age / spacing;
      const float within = phase - static_cast<float>(static_cast<int32_t>(phase));
      return 1.f - within;
    }
    const float last_age = age - first_span;
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

  float analogNoise()
  {
    analog_state_ = analog_state_ * 1664525U + 1013904223U;
    return (static_cast<float>(analog_state_) * (1.f / 2147483648.f)) - 1.f;
  }

  float lfsrNoise()
  {
    const uint32_t bit = ((lfsr_ >> 30) ^ (lfsr_ >> 12)) & 1U;
    lfsr_ = ((lfsr_ << 1) | bit) & 0x7FFFFFFFu;
    if (lfsr_ == 0U)
      lfsr_ = 0x7FFFFFFFu;
    return (lfsr_ & 1U) ? 1.f : -1.f;
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

    triggerVoice(1.f);
    if (dens_norm_ > 0.82f && isBackbeat(step_index) && bpm_ > 0.f)
    {
      const float samples_per_16th = getSampleRate() * 60.f / (bpm_ * 4.f);
      flam_samples_left_ = static_cast<int32_t>(samples_per_16th * 0.5f);
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

  void advancePendingFlam()
  {
    if (flam_samples_left_ <= 0)
      return;
    --flam_samples_left_;
    if (flam_samples_left_ == 0)
      triggerVoice(0.72f);
  }

  void triggerVoice(float accent)
  {
    Voice &voice = voices_[next_voice_index_];
    next_voice_index_ = (next_voice_index_ + 1U) % kVoiceCount;
    voice.active = true;
    voice.age = 0.f;
    voice.accent = accent;
    ++trigger_count_;
  }

  float renderVoice(Voice &voice, float noise, float crack_f, float crack_damp, float room_f, float room_damp,
                    float spacing, float tail_tau, float tail_mix, float type_norm)
  {
    const float burst = burstEnv(voice.age, spacing) * voice.accent;
    const float tail = fasterexpf(-voice.age / tail_tau);
    const float crack = bandpass(noise, crack_f, crack_damp, voice.crack_low, voice.crack_band);
    const float room = bandpass(noise, room_f, room_damp, voice.room_low, voice.room_band);

    const float eight_oh_eight = crack * (burst + tail * tail_mix);
    const float nine_oh_nine = crack * burst + room * tail * tail_mix;
    const float sample = eight_oh_eight + type_norm * (nine_oh_nine - eight_oh_eight);

    voice.age += 1.f / getSampleRate();
    if (voice.age > spacing * 5.f + tail_tau * 6.f)
      voice.active = false;

    return sample * kVoiceGain;
  }

  float renderVoices(float noise)
  {
    const float type_norm = type_norm_;
    const float tone = tone_norm_ * 2.f - 1.f;
    const float tone_ratio = fasterpow2f(tone * 0.55f);
    const float snap = snap_norm_;
    const float decay = 0.45f + decay_norm_ * 1.7f;

    const float crack_hz = (1000.f + type_norm * 400.f) * tone_ratio;
    const float room_hz = (1000.f - type_norm * 150.f) * (0.92f + tone_norm_ * 0.16f);
    const float crack_q = 1.15f + type_norm * 0.55f;
    const float room_q = 1.15f - type_norm * 0.4f;
    const float spacing = 0.010f + type_norm * 0.002f;
    const float tail_tau = (0.100f + type_norm * 0.030f) * decay;
    const float tail_mix = (0.30f + type_norm * 0.12f) * (1.15f - snap * 0.7f);

    const float crack_f = svfF(crack_hz);
    const float room_f = svfF(room_hz);
    const float crack_damp = 1.f / crack_q;
    const float room_damp = 1.f / room_q;

    float sum = 0.f;
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
    {
      Voice &voice = voices_[voiceIndex];
      if (!voice.active)
        continue;
      sum += renderVoice(voice, noise, crack_f, crack_damp, room_f, room_damp, spacing, tail_tau, tail_mix,
                         type_norm);
    }

    const float blocked = sum - dc_prev_in_ + kDcCoeff * dc_prev_out_;
    dc_prev_in_ = sum;
    dc_prev_out_ = blocked;
    return fx::softclip(blocked);
  }

  Voice voices_[kVoiceCount];
  uint32_t next_voice_index_ = 0U;
  uint32_t tick_counter_ = 0U;
  uint32_t analog_state_ = 1U;
  uint32_t lfsr_ = 0x7FFFFFFFu;
  uint32_t trigger_count_ = 0U;
  int32_t flam_samples_left_ = 0;
  float dens_norm_ = 0.f;
  float type_norm_ = 0.f;
  float tone_norm_ = 0.5f;
  float decay_norm_ = 0.5f;
  float snap_norm_ = 0.5f;
  float mix_ = 1.f;
  float bpm_ = 120.f;
  float internal_tick_phase_ = 0.f;
  float analog_lp_ = 0.f;
  float analog_color_coeff_ = 0.7f;
  float dc_prev_in_ = 0.f;
  float dc_prev_out_ = 0.f;
  bool running_ = false;
  bool use_host_clock_ = false;
};
