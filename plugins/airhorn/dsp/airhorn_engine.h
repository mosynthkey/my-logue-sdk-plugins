#pragma once

/*
 * File: airhorn_engine.h
 *
 * Fixed-pitch air horn: 16-bit loop (or one-shot) plus a pitch envelope
 * that recreates the initial pitch drop, then holds the settled tone.
 */

#include "airhorn_pcm.h"
#include <stdint.h>

struct AirHornVoice
{
  bool active = false;
  bool gated = false;
  bool releasing = false;
  uint8_t horn_index = 0U;
  uint8_t note = 0xFF;
  float pos = 0.f;
  float gain = 1.f;
  float amp = 0.f;
  float pitch_ratio = 1.f;
  float sustain_lpf = 0.f;
  uint32_t age = 0U;

  static constexpr float kAttackInc = 1.f / (48000.f * 0.004f);
  static constexpr float kReleaseCoeff = 0.999792f; // ~100 ms
  static constexpr float kMinAmp = 0.0005f;
  static constexpr uint32_t kMinHold = 19200U; // 400 ms at 48 kHz
  static constexpr float kBaseRate = static_cast<float>(kAirhornSampleRate) / 48000.f;
  static constexpr float kLoopCrossfade = 96.f; // embedded-rate samples
  static constexpr float kSustainLpfCoeff = 0.42f; // ~7.5 kHz at 48 kHz
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

  static uint32_t clampIndex(int32_t index, uint32_t length)
  {
    if (index < 0)
      return 0U;
    if (index >= static_cast<int32_t>(length))
      return length - 1U;
    return static_cast<uint32_t>(index);
  }

  static float linearSampleAt(const AirhornSample &horn, float position)
  {
    const int32_t index = static_cast<int32_t>(position);
    const float frac = position - static_cast<float>(index);
    uint32_t i0;
    uint32_t i1;
    if (horn.looping)
    {
      i0 = wrapIndex(index, horn.length);
      i1 = wrapIndex(index + 1, horn.length);
    }
    else
    {
      i0 = clampIndex(index, horn.length);
      i1 = clampIndex(index + 1, horn.length);
    }

    const uint32_t offset = horn.offset;
    const float y0 = pcmToFloat(kAirhornPcm16[offset + i0]);
    const float y1 = pcmToFloat(kAirhornPcm16[offset + i1]);
    return y0 + frac * (y1 - y0);
  }

  static float sampleAt(const AirhornSample &horn, float position, bool sustain_phase)
  {
    if (sustain_phase)
      return linearSampleAt(horn, position);

    const int32_t index = static_cast<int32_t>(position);
    const float frac = position - static_cast<float>(index);
    uint32_t i0;
    uint32_t i1;
    uint32_t i2;
    uint32_t i3;
    if (horn.looping)
    {
      i0 = wrapIndex(index - 1, horn.length);
      i1 = wrapIndex(index, horn.length);
      i2 = wrapIndex(index + 1, horn.length);
      i3 = wrapIndex(index + 2, horn.length);
    }
    else
    {
      i0 = clampIndex(index - 1, horn.length);
      i1 = clampIndex(index, horn.length);
      i2 = clampIndex(index + 1, horn.length);
      i3 = clampIndex(index + 2, horn.length);
    }

    const uint32_t offset = horn.offset;
    return hermite(
        pcmToFloat(kAirhornPcm16[offset + i0]),
        pcmToFloat(kAirhornPcm16[offset + i1]),
        pcmToFloat(kAirhornPcm16[offset + i2]),
        pcmToFloat(kAirhornPcm16[offset + i3]),
        frac);
  }

  static float loopSampleWithCrossfade(const AirhornSample &horn, float position, bool sustain_phase)
  {
    const float loop_length = static_cast<float>(horn.length);
    float output = sampleAt(horn, position, sustain_phase);

    if (!horn.looping || kLoopCrossfade <= 1.f)
      return output;

    const float dist_to_end = loop_length - position;
    if (dist_to_end <= 0.f || dist_to_end >= kLoopCrossfade)
      return output;

    const float blend = 1.f - dist_to_end / kLoopCrossfade;
    const float wrapped = sampleAt(horn, position - loop_length, sustain_phase);
    return output * (1.f - blend) + wrapped * blend;
  }

  bool pitchSettled(const AirhornSample &horn) const
  {
    return horn.looping && (pitch_ratio > 1.f - kPitchSettled) && (pitch_ratio < 1.f + kPitchSettled);
  }

  void trigger(uint8_t horn, uint8_t velocity, uint8_t midi_note)
  {
    if (horn >= kAirhornCount)
      horn = static_cast<uint8_t>(kAirhornCount - 1U);

    active = true;
    gated = true;
    releasing = false;
    horn_index = horn;
    note = midi_note;
    pos = 0.f;
    amp = 0.f;
    sustain_lpf = 0.f;
    age = 0U;
    gain = (static_cast<float>(velocity) + 1.f) * (1.f / 128.f);
    pitch_ratio = kAirhornSamples[horn].start_ratio;
    if (pitch_ratio < 0.5f)
      pitch_ratio = 0.5f;
    if (pitch_ratio > 2.f)
      pitch_ratio = 2.f;
  }

  void releaseGate()
  {
    gated = false;
  }

  float render()
  {
    if (!active)
      return 0.f;

    const AirhornSample &horn = kAirhornSamples[horn_index];

    ++age;
    if (!gated && !releasing && horn.looping && age >= kMinHold)
      releasing = true;

    if (releasing)
    {
      amp *= kReleaseCoeff;
      if (amp < kMinAmp)
      {
        active = false;
        amp = 0.f;
        return 0.f;
      }
    }
    else
    {
      amp += kAttackInc;
      if (amp > 1.f)
        amp = 1.f;
    }

    const bool sustain_phase = pitchSettled(horn);
    float output = loopSampleWithCrossfade(horn, pos, sustain_phase) * gain * amp;

    if (sustain_phase)
    {
      sustain_lpf += kSustainLpfCoeff * (output - sustain_lpf);
      output = sustain_lpf;
    }
    else
    {
      sustain_lpf = output;
    }

    pos += kBaseRate * pitch_ratio;
    if (horn.looping)
    {
      const float loop_length = static_cast<float>(horn.length);
      while (pos >= loop_length)
        pos -= loop_length;
    }
    else if (pos >= static_cast<float>(horn.length - 2U))
    {
      pos = static_cast<float>(horn.length - 2U);
      releasing = true;
    }

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
    amp = 0.f;
    sustain_lpf = 0.f;
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
    TYPE = 0U,
    LEVEL,
    MIX,
    NUM_PARAMS
  };

  enum
  {
    HORN_DJ = 0,
    HORN_TRAIN,
    HORN_BIKE,
    NUM_HORNS
  };

  void init()
  {
    horn_index_ = HORN_DJ;
    level_ = 1.f;
    mix_ = 1.f;
    next_voice_ = 0U;
    clearVoices();
  }

  void reset() { clearVoices(); }

  void setParameter(uint8_t index, int32_t value)
  {
    switch (index)
    {
    case TYPE:
    {
      uint32_t horn = static_cast<uint32_t>(value);
      if (horn >= kAirhornCount)
        horn = kAirhornCount - 1;
      horn_index_ = static_cast<uint8_t>(horn);
      break;
    }
    case LEVEL:
      level_ = param10BitToFloat(value);
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
    static const char *horn_names[NUM_HORNS] = {"DJ", "TRAIN", "BIKE"};

    if (index == TYPE && value >= 0 && static_cast<uint32_t>(value) < kAirhornCount)
      return horn_names[value];

    return nullptr;
  }

  void startVoice(uint8_t horn, uint8_t velocity, uint8_t note)
  {
    voices_[next_voice_].trigger(horn, velocity, note);
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
      wet += voices_[voiceIndex].render();
    return wet;
  }

  uint8_t hornIndex() const { return horn_index_; }

  float outputLevel() const { return level_ * kOutputGain; }

  float mix() const { return mix_; }

private:
  static float param10BitToFloat(int32_t value)
  {
    return static_cast<uint16_t>(value) * 9.77517106549365e-004f;
  }

  void clearVoices()
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
      voices_[voiceIndex].reset();
  }

  uint8_t horn_index_ = HORN_DJ;
  uint8_t next_voice_ = 0U;
  float level_ = 1.f;
  float mix_ = 1.f;
  mutable AirHornVoice voices_[kMaxVoices];
};
