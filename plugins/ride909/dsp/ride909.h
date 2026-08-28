#pragma once

/*
 * File: ride909.h
 *
 * Tempo-synced TR-909 ride cymbal layer for NTS-3.
 * Tap toggles the classic 3-7-10-14 sixteenth-note pattern.
 * X controls pitch (center = normal), Y controls level.
 *
 */

#include "processor.h"
#include "macros.h"
#include "ride909_sample.h"
#include "runtime.h"
#include <math.h>
#include <stdint.h>

class Ride909 : public Processor
{
public:
  static constexpr float kTwoPi = 6.283185307179586f;
  static constexpr uint32_t kVoiceCount = 4U;
  static constexpr uint32_t kStepsPerBar = 16U;
  static constexpr float kPitchRangeSemitones = 12.f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    PITCH = 0U,
    VOL,
    MIX,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case PITCH:
      pitch_norm_ = (static_cast<float>(value) - 512.f) * (1.f / 512.f);
      updatePitchRatio();
      break;
    case VOL:
      volume_ = param_10bit_to_f32(value);
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
    (void)index;
    (void)value;
    return nullptr;
  }

  void init(float *) override final
  {
    pitch_norm_ = 0.f;
    volume_ = 0.8f;
    mix_ = 1.f;
    bpm_ = 120.f;
    running_ = false;
    use_host_clock_ = false;
    tick_counter_ = 0U;
    internal_tick_phase_ = 0.f;
    updatePitchRatio();
    resetVoices();
  }

  void reset() override final
  {
    running_ = false;
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

  void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    (void)id;
    (void)x;
    (void)y;

    if (phase == k_unit_touch_phase_began)
      running_ = !running_;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    if (!use_host_clock_)
      advanceInternalClock(frames);

    const float wet_gain = volume_ * mix_;
    const float dry_gain = 1.f - mix_;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      const float wet = renderVoices();
      const float mixed_left = in[0] * dry_gain + wet * wet_gain;
      const float mixed_right = in[1] * dry_gain + wet * wet_gain;
      out[0] = mixed_left;
      out[1] = mixed_right;
      in += 2;
      out += 2;
    }
  }

private:
  struct Voice
  {
    float position = 0.f;
    float increment = 1.f;
    bool active = false;
  };

  static bool isPatternStep(uint32_t step)
  {
    // Classic techno ride on steps 3, 7, 10, 14 (1-based).
    switch (step)
    {
    case 2U:
    case 6U:
    case 9U:
    case 13U:
      return true;
    default:
      return false;
    }
  }

  void updatePitchRatio()
  {
    pitch_ratio_ = powf(2.f, pitch_norm_ * (kPitchRangeSemitones / 12.f));
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

    const uint32_t step = counter % kStepsPerBar;
    if (isPatternStep(step))
      triggerRide();
  }

  void advanceInternalClock(uint32_t frames)
  {
    if (!running_ || bpm_ <= 0.f)
      return;

    const float sample_rate = getSampleRate();
    const float samples_per_tick = sample_rate * 60.f / (bpm_ * 4.f);
    internal_tick_phase_ += static_cast<float>(frames);

    while (internal_tick_phase_ >= samples_per_tick)
    {
      internal_tick_phase_ -= samples_per_tick;
      ++tick_counter_;
      handleTick(tick_counter_);
    }
  }

  void triggerRide()
  {
    Voice &voice = voices_[next_voice_index_];
    next_voice_index_ = (next_voice_index_ + 1U) % kVoiceCount;
    voice.position = 0.f;
    voice.increment = pitch_ratio_ * (kRide909SampleRate / getSampleRate());
    voice.active = true;
  }

  static float decodeUlaw(uint8_t encoded)
  {
    const uint8_t inverted = static_cast<uint8_t>(~encoded);
    const int32_t exponent = (inverted >> 4) & 0x07;
    const int32_t mantissa = inverted & 0x0F;
    int32_t magnitude = ((0x84 + (mantissa << 3)) << exponent) - 0x84;
    if (inverted & 0x80)
      magnitude = -magnitude;
    return magnitude * (1.f / 32768.f);
  }

  float readSampleLinear(float position) const
  {
    if (position < 0.f)
      return 0.f;

    const uint32_t sample_index = static_cast<uint32_t>(position);
    if (sample_index + 1U >= kRide909SampleLength)
      return 0.f;

    const float fraction = position - static_cast<float>(sample_index);
    const float sample_a = decodeUlaw(kRide909SampleData[sample_index]);
    const float sample_b = decodeUlaw(kRide909SampleData[sample_index + 1U]);
    return sample_a + (sample_b - sample_a) * fraction;
  }

  float renderVoices()
  {
    float sum = 0.f;

    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
    {
      Voice &voice = voices_[voiceIndex];
      if (!voice.active)
        continue;

      sum += readSampleLinear(voice.position);
      voice.position += voice.increment;

      if (voice.position >= static_cast<float>(kRide909SampleLength - 1U))
        voice.active = false;
    }

    return sum;
  }

  Voice voices_[kVoiceCount];
  uint32_t next_voice_index_ = 0U;
  uint32_t tick_counter_ = 0U;
  float pitch_norm_ = 0.f;
  float pitch_ratio_ = 1.f;
  float volume_ = 0.8f;
  float mix_ = 1.f;
  float bpm_ = 120.f;
  float internal_tick_phase_ = 0.f;
  bool running_ = false;
  bool use_host_clock_ = false;
};
