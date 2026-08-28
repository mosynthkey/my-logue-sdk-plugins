#pragma once

/*
 * File: ride909.h
 *
 * Tempo-synced TR-909 ride cymbal layer for NTS-3.
 * Tap-and-hold gates a tempo-synced ride on steps 3-7-11-15.
 * Steps 1-5-9-13 duck the ride (sidechain-style amp curve, kick simulation).
 * X controls pitch (center = normal) via granular overlap-add; decay length stays fixed.
 * Y controls duck depth (bottom = off, top = deepest).
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
  static constexpr uint32_t kVoiceCount = 4U;
  static constexpr uint32_t kStepsPerBar = 16U;
  static constexpr float kPitchRangeSemitones = 12.f;
  static constexpr uint32_t kGrainOutputSize = 256U;
  static constexpr uint32_t kGrainHop = 128U;
  static constexpr uint32_t kGrainsPerVoice = 2U;
  static constexpr float kMaxDuckDepth = 0.45f;
  static constexpr float kDuckRelease = 0.0015f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    PITCH = 0U,
    CURVE,
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
    case CURVE:
      curve_amount_ = param_10bit_to_f32(value);
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
    curve_amount_ = 0.f;
    duck_gain_ = 1.f;
    base_level_ = 0.8f;
    mix_ = 1.f;
    bpm_ = 120.f;
    running_ = false;
    use_host_clock_ = false;
    tick_counter_ = 0U;
    internal_tick_phase_ = 0.f;
    source_rate_ratio_ = kRide909SampleRate / getSampleRate();
    output_length_ = static_cast<uint32_t>(kRide909SampleLength / source_rate_ratio_ + 0.5f);
    updatePitchRatio();
    resetVoices();
  }

  void reset() override final
  {
    running_ = false;
    duck_gain_ = 1.f;
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

    if (phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
        phase == k_unit_touch_phase_stationary)
    {
      running_ = true;
      return;
    }

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      running_ = false;
      resetVoices();
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    if (!use_host_clock_)
      advanceInternalClock(frames);

    const float dry_gain = 1.f - mix_;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      advanceDuckEnvelope();
      const float wet = renderVoices() * base_level_ * mix_ * shapedDuckGain();
      const float mixed_left = in[0] * dry_gain + wet;
      const float mixed_right = in[1] * dry_gain + wet;
      out[0] = mixed_left;
      out[1] = mixed_right;
      in += 2;
      out += 2;
    }
  }

private:
  struct Grain
  {
    uint32_t out_start = 0U;
    float src_start = 0.f;
    bool active = false;
  };

  struct Voice
  {
    bool active = false;
    uint32_t output_index = 0U;
    uint32_t next_grain_out_start = 0U;
    uint32_t spawned_grain_count = 0U;
    float pitch_ratio = 1.f;
    Grain grains[kGrainsPerVoice];
  };

  static uint32_t stepOneBased(uint32_t counter)
  {
    return ((counter - 1U) % kStepsPerBar) + 1U;
  }

  static bool isDuckStep(uint32_t counter)
  {
    switch (stepOneBased(counter))
    {
    case 1U:
    case 5U:
    case 9U:
    case 13U:
      return true;
    default:
      return false;
    }
  }

  static bool isRideStep(uint32_t counter)
  {
    switch (stepOneBased(counter))
    {
    case 3U:
    case 7U:
    case 11U:
    case 15U:
      return true;
    default:
      return false;
    }
  }

  static float grainWindow(float phase)
  {
    if (phase <= 0.f || phase >= 1.f)
      return 0.f;
    // Parabolic window avoids pulling cosf into the 32 KB unit budget.
    return 4.f * phase * (1.f - phase);
  }

  float shapedDuckGain() const
  {
    return duck_gain_;
  }

  void triggerDuck()
  {
    if (curve_amount_ <= 0.f)
      return;

    duck_gain_ = 1.f - curve_amount_ * kMaxDuckDepth;
  }

  void advanceDuckEnvelope()
  {
    if (duck_gain_ >= 1.f)
    {
      duck_gain_ = 1.f;
      return;
    }

    const float release = kDuckRelease * (1.f + curve_amount_ * 3.f);
    duck_gain_ += release;
    if (duck_gain_ > 1.f)
      duck_gain_ = 1.f;
  }

  void updatePitchRatio()
  {
    pitch_ratio_ = powf(2.f, pitch_norm_ * (kPitchRangeSemitones / 12.f));
  }

  void resetVoiceGrains(Voice &voice)
  {
    for (uint32_t grainIndex = 0; grainIndex < kGrainsPerVoice; ++grainIndex)
      voice.grains[grainIndex] = Grain{};
    voice.next_grain_out_start = 0U;
    voice.spawned_grain_count = 0U;
    voice.output_index = 0U;
  }

  void resetVoices()
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
    {
      voices_[voiceIndex] = Voice{};
      resetVoiceGrains(voices_[voiceIndex]);
    }
    next_voice_index_ = 0U;
  }

  void handleTick(uint32_t counter)
  {
    tick_counter_ = counter;
    if (!running_)
      return;

    if (isDuckStep(counter))
      triggerDuck();

    if (isRideStep(counter))
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

  Grain &allocateGrain(Voice &voice)
  {
    Grain &grain = voice.grains[voice.spawned_grain_count % kGrainsPerVoice];
    grain.active = true;
    return grain;
  }

  void spawnGrains(Voice &voice)
  {
    while (voice.next_grain_out_start < voice.output_index + kGrainOutputSize &&
           voice.next_grain_out_start < output_length_)
    {
      Grain &grain = allocateGrain(voice);
      grain.out_start = voice.next_grain_out_start;
      grain.src_start = static_cast<float>(voice.spawned_grain_count) * static_cast<float>(kGrainHop) *
                        voice.pitch_ratio * source_rate_ratio_;
      voice.next_grain_out_start += kGrainHop;
      ++voice.spawned_grain_count;
    }
  }

  void triggerRide()
  {
    Voice &voice = voices_[next_voice_index_];
    next_voice_index_ = (next_voice_index_ + 1U) % kVoiceCount;
    resetVoiceGrains(voice);
    voice.active = true;
    voice.pitch_ratio = pitch_ratio_;
    spawnGrains(voice);
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

  float renderVoice(Voice &voice)
  {
    spawnGrains(voice);

    float weighted_sum = 0.f;
    float weight_sum = 0.f;
    const float source_step = voice.pitch_ratio * source_rate_ratio_;

    for (uint32_t grainIndex = 0; grainIndex < kGrainsPerVoice; ++grainIndex)
    {
      const Grain &grain = voice.grains[grainIndex];
      if (!grain.active)
        continue;

      if (voice.output_index < grain.out_start)
        continue;

      const uint32_t local_output = voice.output_index - grain.out_start;
      if (local_output >= kGrainOutputSize)
        continue;

      const float window = grainWindow(static_cast<float>(local_output) /
                                       static_cast<float>(kGrainOutputSize - 1U));
      const float source_position = grain.src_start + static_cast<float>(local_output) * source_step;
      weighted_sum += window * readSampleLinear(source_position);
      weight_sum += window;
    }

    ++voice.output_index;
    if (voice.output_index >= output_length_)
      voice.active = false;

    if (weight_sum <= 0.f)
      return 0.f;

    return weighted_sum / weight_sum;
  }

  float renderVoices()
  {
    float sum = 0.f;

    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
    {
      Voice &voice = voices_[voiceIndex];
      if (!voice.active)
        continue;

      sum += renderVoice(voice);
    }

    return sum;
  }

  Voice voices_[kVoiceCount];
  uint32_t next_voice_index_ = 0U;
  uint32_t tick_counter_ = 0U;
  uint32_t output_length_ = 0U;
  float pitch_norm_ = 0.f;
  float pitch_ratio_ = 1.f;
  float source_rate_ratio_ = 1.f;
  float curve_amount_ = 0.f;
  float duck_gain_ = 1.f;
  float base_level_ = 0.8f;
  float mix_ = 1.f;
  float bpm_ = 120.f;
  float internal_tick_phase_ = 0.f;
  bool running_ = false;
  bool use_host_clock_ = false;
};
