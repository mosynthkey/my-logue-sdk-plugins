#pragma once

/*
 * File: airhorn.h
 *
 * Fixed-pitch air horn sample player. Any MIDI note triggers the selected
 * horn at its original recorded pitch (no keyboard tracking).
 */

#include "airhorn_pcm.h"
#include "processor.h"
#include "macros.h"
#include <math.h>
#include <stdint.h>

struct AirHornVoice
{
  bool active = false;
  float pos = 0.f;
  uint8_t horn_index = 0U;
  float gain = 1.f;

  static float pcmToFloat(uint8_t code)
  {
    return (static_cast<float>(code) - 128.f) * (1.f / 128.f);
  }

  static float sampleAt(uint8_t horn, float position)
  {
    const AirhornSample &horn_sample = kAirhornSamples[horn];
    if (position < 0.f)
      return 0.f;

    const uint32_t last_index = horn_sample.length - 1U;
    const uint32_t index_a = static_cast<uint32_t>(position);
    if (index_a >= last_index)
      return pcmToFloat(kAirhornPcm8[horn_sample.offset + last_index]);

    const float frac = position - static_cast<float>(index_a);
    const uint32_t offset = horn_sample.offset;
    const float sample_a = pcmToFloat(kAirhornPcm8[offset + index_a]);
    const float sample_b = pcmToFloat(kAirhornPcm8[offset + index_a + 1U]);
    return sample_a + (sample_b - sample_a) * frac;
  }

  void trigger(uint8_t horn, uint8_t velocity)
  {
    if (horn >= kAirhornCount)
      horn = static_cast<uint8_t>(kAirhornCount - 1U);

    active = true;
    pos = 0.f;
    horn_index = horn;
    gain = (static_cast<float>(velocity) + 1.f) * (1.f / 128.f);
  }

  float render(float playback_rate)
  {
    if (!active)
      return 0.f;

    const AirhornSample &horn_sample = kAirhornSamples[horn_index];
    const float output = sampleAt(horn_index, pos) * gain;
    pos += playback_rate;

    if (pos >= static_cast<float>(horn_sample.length - 1U))
      active = false;

    return output;
  }

  void reset() { active = false; }
};

class AirHorn : public Processor
{
public:
  static constexpr uint32_t kMaxVoices = 8U;
  static constexpr float kPlaybackRate = static_cast<float>(kAirhornSampleRate) / getSampleRate();
  static constexpr float kOutputGain = 0.85f;

  uint32_t getBufferSize() const override final { return 0; }

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

  void setStereoMix(bool enabled) { stereo_mix_ = enabled; }

  void setParameter(uint8_t index, int32_t value) override final
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
      level_ = param_10bit_to_f32(value);
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

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    static const char *horn_names[NUM_HORNS] = {"DJ", "TRAIN", "BIKE"};

    if (index == TYPE && value >= 0 && static_cast<uint32_t>(value) < kAirhornCount)
      return horn_names[value];

    return nullptr;
  }

  void init(float *) override final
  {
    horn_index_ = HORN_DJ;
    level_ = 1.f;
    mix_ = 1.f;
    next_voice_ = 0U;
    clearVoices();
  }

  void reset() override final { clearVoices(); }

  void setPitch(float w0)
  {
    (void)w0;
  }

  void noteOn(uint8_t note, uint8_t velo) override final
  {
    (void)note;
    startVoice(horn_index_, velo);
  }

  void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    (void)id;
    (void)x;
    (void)y;

    if (phase == 0U)
      startVoice(horn_index_, 127);
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    const float wet_gain = level_ * kOutputGain;
    const float wet_amt = stereo_mix_ ? mix_ : 1.f;
    const float dry_amt = stereo_mix_ ? (1.f - mix_) : 0.f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float wet = 0.f;
      for (uint32_t voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
        wet += voices_[voiceIndex].render(kPlaybackRate);

      wet *= wet_gain;

      if (stereo_mix_)
      {
        out[0] = in[0] * dry_amt + wet * wet_amt;
        out[1] = in[1] * dry_amt + wet * wet_amt;
        in += 2;
        out += 2;
      }
      else
      {
        out[0] = wet;
        ++out;
      }
    }
  }

  void startVoice(uint8_t horn, uint8_t velocity)
  {
    voices_[next_voice_].trigger(horn, velocity);
    next_voice_ = (next_voice_ + 1U) % kMaxVoices;
  }

  uint8_t hornIndex() const { return horn_index_; }

  float outputLevel() const { return level_ * kOutputGain; }

private:
  void clearVoices()
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
      voices_[voiceIndex].reset();
  }

  bool stereo_mix_ = false;
  uint8_t horn_index_ = HORN_DJ;
  uint8_t next_voice_ = 0U;
  float level_ = 1.f;
  float mix_ = 1.f;
  mutable AirHornVoice voices_[kMaxVoices];
};
