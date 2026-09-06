#pragma once

/*
 * File: snarerush.h
 *
 * Exponential snare-roll build. Touch starts a rush that halves its hit
 * interval as it approaches the end of the bar window; lift fires a last
 * accent and lets the envelope die.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class SnareRush : public Processor
{
public:
  static constexpr uint32_t kVoiceCount = 6U;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    BARS = 0U,
    TONE,
    MIX,
    TUNE,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case BARS:
      bars_norm_ = param_10bit_to_f32(value);
      break;
    case TONE:
      tone_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case TUNE:
      tune_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *) override final
  {
    bpm_ = 120.f;
    rush_pos_ = 0.f;
    next_hit_ = 0.f;
    rng_ = 31U;
    next_voice_ = 0U;
    running_ = false;
    firing_ = false;
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
      voices_[voiceIndex] = Voice{};
  }

  void reset() override final
  {
    rush_pos_ = 0.f;
    next_hit_ = 0.f;
    running_ = false;
    firing_ = false;
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
      voices_[voiceIndex] = Voice{};
  }

  void setTempo(float tempo) override final
  {
    if (tempo >= 40.f && tempo <= 300.f)
      bpm_ = tempo;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began)
    {
      rush_pos_ = 0.f;
      next_hit_ = 0.f;
      running_ = true;
      firing_ = true;
      return;
    }
    if (phase == k_unit_touch_phase_moved || phase == k_unit_touch_phase_stationary)
    {
      running_ = true;
      return;
    }
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      if (firing_)
        trigger(1.35f);
      firing_ = false;
      running_ = false;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    (void)raw;
    const float beat = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()));
    const float eighth = beat * 0.125f;
    const float duration = (2.f + bars_norm_ * 6.f) * beat * 4.f;
    const float inv_sr = 1.f / getSampleRate();
    const float body_hz = 140.f + tune_norm_ * 140.f;
    const float hp_hz = 800.f + tone_norm_ * 4200.f;
    const float hp_coeff = fx::onePoleCoeff(hp_hz, getSampleRate());
    const float noise_decay = 0.992f - (1.f - tone_norm_) * 0.012f;
    const float body_decay = 0.988f - tone_norm_ * 0.004f;
    const float wash = 0.22f + tone_norm_ * 0.55f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      if (running_ && firing_)
      {
        if (rush_pos_ >= next_hit_)
        {
          const float progress = fx::clip01(rush_pos_ / duration);
          trigger(0.72f + progress * 0.45f);
          const float interval = eighth * fasterpow2f(-progress * 4.f);
          next_hit_ = rush_pos_ + fx::clip(interval, 8.f, eighth);
        }
        rush_pos_ += 1.f;
      }

      float acc = 0.f;
      for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
      {
        Voice &voice = voices_[voiceIndex];
        if (!voice.active)
          continue;

        voice.phase = fx::wrap01(voice.phase + body_hz * inv_sr);
        const float body = fastersinfullf(voice.phase * 6.283185307179586f) * voice.body_env;
        const float white = fx::randomFloat(rng_) * 2.f - 1.f;
        voice.hp_z += hp_coeff * (white - voice.hp_z);
        const float snap = (white - voice.hp_z) * voice.noise_env;
        acc += (body * 0.55f + snap * wash) * voice.accent;

        voice.body_env *= body_decay;
        voice.noise_env *= noise_decay;
        if (voice.body_env < 0.0008f && voice.noise_env < 0.0008f)
          voice.active = false;
      }

      const float wet = fastertanhf(acc * 1.25f);
      out[0] = fx::mix(in[0], wet, mix_);
      out[1] = fx::mix(in[1], wet, mix_);
      in += 2;
      out += 2;
    }
  }

private:
  struct Voice
  {
    bool active = false;
    float accent = 1.f;
    float phase = 0.f;
    float body_env = 0.f;
    float noise_env = 0.f;
    float hp_z = 0.f;
  };

  void trigger(float accent)
  {
    Voice &voice = voices_[next_voice_];
    next_voice_ = (next_voice_ + 1U) % kVoiceCount;
    voice.active = true;
    voice.accent = accent;
    voice.phase = 0.f;
    voice.body_env = 1.f;
    voice.noise_env = 1.f;
    voice.hp_z = 0.f;
  }

  Voice voices_[kVoiceCount];
  uint32_t next_voice_ = 0U;
  uint32_t rng_ = 31U;
  float rush_pos_ = 0.f;
  float next_hit_ = 0.f;
  float bpm_ = 120.f;
  float bars_norm_ = 0.39f;
  float tone_norm_ = 0.34f;
  float tune_norm_ = 0.47f;
  float mix_ = 1.f;
  bool running_ = false;
  bool firing_ = false;
};
