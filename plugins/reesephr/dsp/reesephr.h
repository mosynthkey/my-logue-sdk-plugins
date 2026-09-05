#pragma once

/*
 * File: reesephr.h
 *
 * Detuned Reese drone with a sparse scale-walk phrase. Touch gates the voice.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class ReesePhr : public Processor
{
public:
  static constexpr uint32_t kVoiceCount = 4U;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    PITCH = 0U,
    DETUN,
    MIX,
    ROOT,
    RATE,
    SUB,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case PITCH:
      pitch_norm_ = param_10bit_to_f32(value);
      break;
    case DETUN:
      detune_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case ROOT:
      root_note_ = static_cast<int8_t>(fx::clip(static_cast<float>(value), 24.f, 48.f));
      break;
    case RATE:
      rate_norm_ = param_10bit_to_f32(value);
      break;
    case SUB:
      sub_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index != ROOT)
      return nullptr;
    static char label[8];
    static const char *kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int32_t note = value;
    if (note < 0)
      note = 0;
    if (note > 127)
      note = 127;
    char *out = label;
    const char *name = kNames[note % 12];
    while (*name)
      *out++ = *name++;
    *out++ = static_cast<char>('0' + (note / 12) - 1);
    *out = '\0';
    return label;
  }

  void init(float *) override final
  {
    bpm_ = 120.f;
    amp_ = 0.f;
    pad_held_ = false;
    clock_acc_ = 0.f;
    walk_degree_ = 0;
    target_degree_ = 0;
    rng_ = 13U;
    sub_phase_ = 0.f;
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
      phase_[voiceIndex] = 0.f;
  }

  void reset() override final
  {
    amp_ = 0.f;
    clock_acc_ = 0.f;
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 40.f && tempo < 300.f)
      bpm_ = tempo;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    pad_held_ = phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
                phase == k_unit_touch_phase_stationary;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    (void)raw;
    const float amp_coeff = 1.f - fasterexpf(-1.f / (pad_held_ ? 120.f : 900.f));
    const float bar_samples = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()) * 4U);
    const float walk_period = bar_samples * (1.2f + (1.f - rate_norm_) * 2.4f);
    static const float kSpread[kVoiceCount] = {-1.f, -0.33f, 0.33f, 1.f};
    const float detune_cents = detune_norm_ * 28.f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      clock_acc_ += 1.f;
      if (pad_held_ && clock_acc_ >= walk_period)
      {
        clock_acc_ = 0.f;
        static const int8_t kWalk[] = {-3, 0, 0, 3, 5, -5};
        target_degree_ = kWalk[fx::nextRandom(rng_) % 6U];
      }
      walk_degree_ += (static_cast<float>(target_degree_) - walk_degree_) * 0.00025f;

      const float base_note = static_cast<float>(root_note_) + pitch_norm_ * 24.f - 6.f + walk_degree_;
      float saw = 0.f;
      for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
      {
        const float note = base_note + kSpread[voiceIndex] * detune_cents * (1.f / 100.f);
        const float inc = fx::noteToInc(note, getSampleRate());
        phase_[voiceIndex] = fx::wrap01(phase_[voiceIndex] + inc);
        saw += fx::blepSaw(phase_[voiceIndex], inc);
      }
      saw *= 0.22f;

      const float sub_inc = fx::noteToInc(base_note - 12.f, getSampleRate());
      sub_phase_ = fx::wrap01(sub_phase_ + sub_inc);
      const float sub = fx::blepPulse(sub_phase_, sub_inc, 0.5f) * sub_norm_ * 0.28f;

      amp_ += ((pad_held_ ? 1.f : 0.f) - amp_) * amp_coeff;
      const float wet = fx::softclip((saw + sub) * amp_ * 1.15f);
      out[0] = fx::mix(in[0], wet, mix_);
      out[1] = fx::mix(in[1], wet * 0.96f + saw * 0.04f, mix_);
      in += 2;
      out += 2;
    }
  }

private:
  float phase_[kVoiceCount] = {};
  float sub_phase_ = 0.f;
  float amp_ = 0.f;
  float clock_acc_ = 0.f;
  float walk_degree_ = 0.f;
  float bpm_ = 120.f;
  float pitch_norm_ = 0.35f;
  float detune_norm_ = 0.51f;
  float rate_norm_ = 0.3f;
  float sub_norm_ = 0.4f;
  float mix_ = 1.f;
  uint32_t rng_ = 13U;
  int8_t root_note_ = 36;
  int8_t target_degree_ = 0;
  bool pad_held_ = false;
};
