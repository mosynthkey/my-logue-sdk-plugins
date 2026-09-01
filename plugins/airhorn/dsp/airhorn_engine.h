#pragma once

/*
 * File: airhorn_engine.h
 *
 * Fixed-pitch DJ air horn: a 16-bit loop plus a pitch envelope that
 * recreates the opening drop, then fades naturally instead of sustaining at
 * full level. The loop wrap is crossfaded in the embedded PCM (see
 * scripts/embed_pcm.py), so playback here is a plain wrapping read.
 *
 * Sustain uses one wrapping player, not a ping-pong pair. A second player with
 * a runtime crossfade would re-blend the loop tail into a head that already
 * contains that tail, which increases level pumping (~3 dB vs ~0.3 dB on the
 * baked loop). See fade_experiment.py --compare-loop-modes.
 */

#include "airhorn_pcm.h"
#include <math.h>
#include <stdint.h>

struct AirHornVoice
{
  bool active = false;
  bool gated = false;
  bool releasing = false;
  bool fading = false;
  uint8_t note = 0xFF;
  float pos = 0.f;
  float gain = 1.f;
  float amp = 0.f;
  float pitch_ratio = 1.f;
  uint32_t settled_age = 0U;

  static constexpr float kAttackInc = 1.f / (48000.f * 0.004f);
  static constexpr float kReleaseDecayCoeff = 0.99994048f; // tau 0.35 s after note off
  static constexpr float kEndDecayCoeff = 0.999792f; // one-shot tail ~100 ms
  static constexpr float kMinAmp = 0.0005f;
  static constexpr uint32_t kSettledHoldSamples = 12000U; // 250 ms at 48 kHz
  static constexpr float kBaseRate = static_cast<float>(kAirhornSampleRate) / 48000.f;
  static constexpr float kPitchSettled = 0.025f;

  static float pcmToFloat(int16_t sample)
  {
    return static_cast<float>(sample) * (1.f / 32768.f);
  }

  static float hermite(float y0, float y1, float y2, float y3, float frac)
  {
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + y1;
  }

  static uint32_t wrapIndex(int32_t index, uint32_t length)
  {
    int32_t wrapped = index % static_cast<int32_t>(length);
    if (wrapped < 0)
      wrapped += static_cast<int32_t>(length);
    return static_cast<uint32_t>(wrapped);
  }

  static float sampleAt(const AirhornSample &horn, float position)
  {
    const int32_t index = static_cast<int32_t>(position);
    const float frac = position - static_cast<float>(index);
    const uint32_t i0 = wrapIndex(index - 1, horn.length);
    const uint32_t i1 = wrapIndex(index, horn.length);
    const uint32_t i2 = wrapIndex(index + 1, horn.length);
    const uint32_t i3 = wrapIndex(index + 2, horn.length);
    const uint32_t offset = horn.offset;
    return hermite(
        pcmToFloat(kAirhornPcm16[offset + i0]),
        pcmToFloat(kAirhornPcm16[offset + i1]),
        pcmToFloat(kAirhornPcm16[offset + i2]),
        pcmToFloat(kAirhornPcm16[offset + i3]),
        frac);
  }

  bool pitchSettled() const
  {
    return (pitch_ratio > 1.f - kPitchSettled) && (pitch_ratio < 1.f + kPitchSettled);
  }

  void trigger(uint8_t velocity, uint8_t midi_note)
  {
    active = true;
    gated = true;
    releasing = false;
    fading = false;
    note = midi_note;
    pos = 0.f;
    amp = 0.f;
    settled_age = 0U;
    gain = (static_cast<float>(velocity) + 1.f) * (1.f / 128.f);
    pitch_ratio = kAirhornSamples[0].start_ratio;
    if (pitch_ratio < 0.5f)
      pitch_ratio = 0.5f;
    if (pitch_ratio > 2.f)
      pitch_ratio = 2.f;
  }

  void releaseGate()
  {
    gated = false;
  }

  float render(float natural_decay_coeff)
  {
    if (!active)
      return 0.f;

    const AirhornSample &horn = kAirhornSamples[0];

    const bool settled = pitchSettled();
    if (settled)
      ++settled_age;
    else
      settled_age = 0U;

    if (settled && settled_age >= kSettledHoldSamples)
      fading = true;

    if (fading)
      amp *= gated ? natural_decay_coeff : kReleaseDecayCoeff;
    else if (releasing)
      amp *= kEndDecayCoeff;
    else
    {
      amp += kAttackInc;
      if (amp > 1.f)
        amp = 1.f;
    }

    if (amp < kMinAmp)
    {
      active = false;
      amp = 0.f;
      return 0.f;
    }

    float output = sampleAt(horn, pos) * gain * amp;

    pos += kBaseRate * pitch_ratio;
    const float loop_length = static_cast<float>(horn.length);
    while (pos >= loop_length)
      pos -= loop_length;

    if (horn.env_coeff > 0.f)
      pitch_ratio = 1.f + (pitch_ratio - 1.f) * horn.env_coeff;

    if (output > 1.f)
      output = 1.f;
    if (output < -1.f)
      output = -1.f;

    return output;
  }

  void reset()
  {
    active = false;
    gated = false;
    releasing = false;
    fading = false;
    amp = 0.f;
  }
};

class AirHornEngine
{
public:
  static constexpr uint32_t kMaxVoices = 8U;
  static constexpr float kHostSampleRate = 48000.f;
  static constexpr float kPlaybackRate = AirHornVoice::kBaseRate;
  static constexpr float kOutputGain = 0.9f;

  enum
  {
    LEVEL = 0U,
    FADE,
    MIX,
    NUM_PARAMS
  };

  void init()
  {
    level_ = 1.f;
    natural_decay_coeff_ = kDefaultNaturalDecayCoeff;
    mix_ = 1.f;
    next_voice_ = 0U;
    clearVoices();
  }

  void reset() { clearVoices(); }

  void setParameter(uint8_t index, int32_t value)
  {
    switch (index)
    {
    case LEVEL:
      level_ = param10BitToFloat(value);
      break;
    case FADE:
      natural_decay_coeff_ = fadeParamToDecayCoeff(value);
      break;
    case MIX:
      mix_ = value / 1000.f;
      if (mix_ < 0.f)
        mix_ = 0.f;
      if (mix_ > 1.f)
        mix_ = 1.f;
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const
  {
    (void)index;
    (void)value;
    return nullptr;
  }

  void startVoice(uint8_t velocity, uint8_t note)
  {
    voices_[next_voice_].trigger(velocity, note);
    next_voice_ = (next_voice_ + 1U) % kMaxVoices;
  }

  void releaseNote(uint8_t note)
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
    {
      if (voices_[voiceIndex].active && voices_[voiceIndex].note == note)
        voices_[voiceIndex].releaseGate();
    }
  }

  void releaseAll()
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
      voices_[voiceIndex].releaseGate();
  }

  float renderMono() const
  {
    float wet = 0.f;
    for (uint32_t voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
      wet += voices_[voiceIndex].render(natural_decay_coeff_);
    return wet;
  }

  float outputLevel() const { return level_ * kOutputGain; }

  float naturalDecayCoeff() const { return natural_decay_coeff_; }

  float mix() const { return mix_; }

private:
  static constexpr float kDefaultNaturalDecayCoeff = 0.99998843f; // tau 1.8 s @ 48 kHz
  static constexpr float kFadeTauMinSec = 0.25f;
  static constexpr float kFadeTauMaxSec = 12.f;

  static float param10BitToFloat(int32_t value)
  {
    return static_cast<uint16_t>(value) * 9.77517106549365e-004f;
  }

  static float fadeParamToDecayCoeff(int32_t value)
  {
    if (value <= 0)
      return 1.f;

    const float norm = static_cast<float>(value) * (1.f / 1023.f);
    const float tau = kFadeTauMinSec * powf(kFadeTauMaxSec / kFadeTauMinSec, 1.f - norm);
    return expf(-1.f / (tau * kHostSampleRate));
  }

  void clearVoices()
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
      voices_[voiceIndex].reset();
  }

  uint8_t next_voice_ = 0U;
  float level_ = 1.f;
  float natural_decay_coeff_ = kDefaultNaturalDecayCoeff;
  float mix_ = 1.f;
  mutable AirHornVoice voices_[kMaxVoices];
};
