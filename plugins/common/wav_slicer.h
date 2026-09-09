#pragma once

/*
 * Shared tempo-synced 1-bar WAV slicer (AmenTime, WavSlice).
 * PCM symbols (kSlicePcm8, kSlicePcmLength) come from the plugin header
 * included before this file.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class WavSlicer : public Processor
{
public:
  static constexpr uint32_t kVoiceCount = 2U;
  static constexpr uint32_t kChunks = 32U;
  static constexpr uint32_t kXfadeSamples = 48U;
  static constexpr float kVoiceGain = 0.72f;
  static constexpr float kPcmScale = 1.f / 127.f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    STRT = 0U,
    SIZE,
    MIX,
    TUNE,
    RPT,
    REVS,
    HOLD,
    NUM_PARAMS
  };

  enum
  {
    RPT_RUN = 0,
    RPT_LOCK
  };

  enum
  {
    HOLD_GATE = 0,
    HOLD_LATCH
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case STRT:
      start_norm_ = param_10bit_to_f32(value);
      break;
    case SIZE:
      size_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case TUNE:
      tune_norm_ = param_10bit_to_f32(value);
      break;
    case RPT:
      rpt_ = (value != 0) ? RPT_LOCK : RPT_RUN;
      break;
    case REVS:
      reverse_norm_ = param_10bit_to_f32(value);
      break;
    case HOLD:
      hold_ = (value != 0) ? HOLD_LATCH : HOLD_GATE;
      if (hold_ == HOLD_GATE && !pad_held_)
      {
        running_ = false;
        fadeOutVoices();
      }
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index == SIZE)
    {
      const float size_norm = param_10bit_to_f32(value);
      if (size_norm < 0.25f)
        return "1/4";
      if (size_norm < 0.50f)
        return "1/8";
      if (size_norm < 0.75f)
        return "1/16";
      return "1/32";
    }
    if (index == RPT)
      return (value != 0) ? "LOCK" : "RUN";
    if (index == HOLD)
      return (value != 0) ? "LATC" : "GATE";
    return nullptr;
  }

  void init(float *) override final
  {
    start_norm_ = 0.f;
    size_norm_ = 0.625f;
    mix_ = 1.f;
    tune_norm_ = 0.5f;
    reverse_norm_ = 0.f;
    rpt_ = RPT_RUN;
    hold_ = HOLD_GATE;
    bpm_ = 120.f;
    rng_ = 0xA5A5A5A5u;
    resetPlayback();
  }

  void reset() override final { resetPlayback(); }

  void setTempo(float tempo) override final
  {
    if (tempo > 40.f && tempo < 300.f)
      bpm_ = tempo;
  }

  void tempo4ppqnTick(uint32_t) override final {}

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
        phase == k_unit_touch_phase_stationary)
    {
      const bool start_now = !pad_held_;
      pad_held_ = true;
      running_ = true;
      if (phase == k_unit_touch_phase_began || start_now)
      {
        clock_acc_ = 0.f;
        step_index_ = 0U;
        fireCurrentSlice();
      }
      return;
    }

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      pad_held_ = false;
      if (hold_ == HOLD_GATE)
      {
        running_ = false;
        fadeOutVoices();
      }
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    const float dry_gain = 1.f - mix_;
    const float slice_samples = samplesPerSlice();

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      if (running_ && slice_samples > 1.f)
      {
        clock_acc_ += 1.f;
        if (clock_acc_ >= slice_samples)
        {
          clock_acc_ -= slice_samples;
          if (rpt_ == RPT_RUN)
            step_index_ = (step_index_ + 1U) % slicesPerBar();
          fireCurrentSlice();
        }
      }

      float wet = 0.f;
      for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
      {
        Voice &voice = voices_[voiceIndex];
        if (!voice.active)
          continue;

        wet += readVoice(voice) * voice.gain * kVoiceGain;
        voice.phase += voice.inc;
        voice.gain = fx::clip01(voice.gain + voice.fade);

        if (voice.length > 1.f)
        {
          while (voice.phase >= voice.length)
            voice.phase -= voice.length;
          while (voice.phase < 0.f)
            voice.phase += voice.length;
        }

        if (voice.gain <= 0.f && voice.fade < 0.f)
          voice.active = false;
      }

      const float mixed = in[0] * dry_gain + wet * mix_;
      out[0] = mixed;
      out[1] = in[1] * dry_gain + wet * mix_;
      in += 2;
      out += 2;
    }
  }

  uint32_t debugTriggerCount() const { return trigger_count_; }
  uint32_t debugSlicesPerBar() const { return slicesPerBar(); }
  bool debugRunning() const { return running_; }

private:
  struct Voice
  {
    bool active = false;
    bool reverse = false;
    float start = 0.f;
    float length = 1.f;
    float phase = 0.f;
    float inc = 0.f;
    float gain = 0.f;
    float fade = 0.f;
  };

  void resetPlayback()
  {
    running_ = false;
    pad_held_ = false;
    clock_acc_ = 0.f;
    step_index_ = 0U;
    next_voice_index_ = 0U;
    trigger_count_ = 0U;
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
      voices_[voiceIndex] = Voice{};
  }

  uint32_t slicesPerBar() const
  {
    if (size_norm_ < 0.25f)
      return 4U;
    if (size_norm_ < 0.50f)
      return 8U;
    if (size_norm_ < 0.75f)
      return 16U;
    return 32U;
  }

  float samplesPerSlice() const
  {
    const float bar_samples = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()) * 4U);
    return bar_samples / static_cast<float>(slicesPerBar());
  }

  float playbackIncrement() const
  {
    const float host_bar = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()) * 4U);
    const float base = static_cast<float>(kSlicePcmLength) / host_bar;
    const float ratio = fasterpow2f((tune_norm_ - 0.5f) * 2.f);
    return base * ratio;
  }

  void fireCurrentSlice()
  {
    const uint32_t slices = slicesPerBar();
    const uint32_t chunks_per_slice = kChunks / slices;
    // STRT is an Edit offset; pad X must not drive this (UKGarage-style step walk).
    const uint32_t start_16th = static_cast<uint32_t>(start_norm_ * 15.999f);
    const uint32_t start_chunk = start_16th * (kChunks / 16U);
    uint32_t source_chunk = start_chunk;
    if (rpt_ == RPT_RUN)
      source_chunk = (start_chunk + step_index_ * chunks_per_slice) % kChunks;

    const float chunk_len = static_cast<float>(kSlicePcmLength) / static_cast<float>(kChunks);
    const float start = static_cast<float>(source_chunk) * chunk_len;
    const float length = static_cast<float>(chunks_per_slice) * chunk_len;
    const bool reverse = fx::randomFloat(rng_) < reverse_norm_;
    startVoice(start, length, reverse);
    ++trigger_count_;
  }

  void fadeOutVoices()
  {
    const float fade_delta = 1.f / static_cast<float>(kXfadeSamples);
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
    {
      Voice &voice = voices_[voiceIndex];
      if (voice.active)
        voice.fade = -fade_delta;
    }
  }

  void startVoice(float start, float length, bool reverse)
  {
    fadeOutVoices();

    Voice &voice = voices_[next_voice_index_];
    next_voice_index_ = (next_voice_index_ + 1U) % kVoiceCount;
    voice.active = true;
    voice.reverse = reverse;
    voice.start = start;
    voice.length = length > 1.f ? length : 1.f;
    voice.inc = playbackIncrement();
    voice.phase = 0.f;
    voice.gain = 0.f;
    voice.fade = 1.f / static_cast<float>(kXfadeSamples);
  }

  static float wrapPcm(float pos)
  {
    const float pcm_length = static_cast<float>(kSlicePcmLength);
    if (pcm_length <= 1.f)
      return 0.f;
    while (pos >= pcm_length)
      pos -= pcm_length;
    while (pos < 0.f)
      pos += pcm_length;
    return pos;
  }

  static float readVoice(const Voice &voice)
  {
    float offset = voice.phase;
    if (voice.reverse)
      offset = voice.length - 1.f - voice.phase;
    const float pos = wrapPcm(voice.start + offset);
    const uint32_t index0 = static_cast<uint32_t>(pos);
    uint32_t index1 = index0 + 1U;
    if (index1 >= kSlicePcmLength)
      index1 = 0U;
    const float frac = pos - static_cast<float>(index0);
    const float sample0 = static_cast<float>(kSlicePcm8[index0]) * kPcmScale;
    const float sample1 = static_cast<float>(kSlicePcm8[index1]) * kPcmScale;
    return sample0 + (sample1 - sample0) * frac;
  }

  Voice voices_[kVoiceCount];
  float start_norm_ = 0.f;
  float size_norm_ = 0.625f;
  float mix_ = 1.f;
  float tune_norm_ = 0.5f;
  float reverse_norm_ = 0.f;
  float bpm_ = 120.f;
  float clock_acc_ = 0.f;
  uint32_t step_index_ = 0U;
  uint32_t next_voice_index_ = 0U;
  uint32_t trigger_count_ = 0U;
  uint32_t rng_ = 1U;
  uint8_t rpt_ = RPT_RUN;
  uint8_t hold_ = HOLD_GATE;
  bool running_ = false;
  bool pad_held_ = false;
};
