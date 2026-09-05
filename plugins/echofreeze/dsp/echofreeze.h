#pragma once

/*
 * File: echofreeze.h
 *
 * Octatrack-style echo freeze. The delay writes until Lock, then the captured
 * window loops at a pitched playback rate with decaying feedback.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class EchoFreeze : public Processor
{
public:
  static constexpr uint32_t kMaxBufSamples = 192000U;
  static constexpr float kMinBpm = 40.f;
  static constexpr float kMaxBpm = 300.f;

  uint32_t getBufferSize() const override final { return kMaxBufSamples * 2U; }

  enum
  {
    SIZE = 0U,
    PITCH,
    MIX,
    DECAY,
    MODE,
    NUM_PARAMS
  };

  enum
  {
    MODE_HOLD = 0,
    MODE_TOGGLE
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case SIZE:
      size_norm_ = param_10bit_to_f32(value);
      break;
    case PITCH:
      pitch_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case DECAY:
      decay_norm_ = param_10bit_to_f32(value);
      break;
    case MODE:
      toggle_mode_ = value != 0;
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index == MODE)
      return (value != 0) ? "TOGL" : "HOLD";
    return nullptr;
  }

  void init(float *allocated_buffer) override final
  {
    left_ = allocated_buffer;
    right_ = allocated_buffer + kMaxBufSamples;
    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;
    write_pos_ = 0U;
    play_pos_ = 0.f;
    locked_ = false;
    pad_held_ = false;
    captured_ = 0U;
    bpm_ = 120.f;
    wet_ = 0.f;
    fade_left_ = 0.f;
    fade_right_ = 0.f;
  }

  void teardown() override final
  {
    left_ = nullptr;
    right_ = nullptr;
  }

  void reset() override final
  {
    write_pos_ = 0U;
    play_pos_ = 0.f;
    locked_ = false;
    captured_ = 0U;
    wet_ = 0.f;
    if (left_ != nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < kMaxBufSamples; ++sampleIndex)
      {
        left_[sampleIndex] = 0.f;
        right_[sampleIndex] = 0.f;
      }
    }
  }

  void setTempo(float tempo) override final
  {
    if (tempo >= kMinBpm && tempo <= kMaxBpm)
      bpm_ = tempo;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    const bool down = phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
                      phase == k_unit_touch_phase_stationary;
    if (toggle_mode_)
    {
      if (phase == k_unit_touch_phase_began)
        setLocked(!locked_);
      return;
    }

    if (down)
    {
      if (!pad_held_)
        setLocked(true);
      pad_held_ = true;
      return;
    }
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      pad_held_ = false;
      setLocked(false);
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const uint32_t delay_samples = delayLength();
    const float rate = playbackRate();
    const float wet_coeff = 1.f - fasterexpf(-1.f / 192.f);
    const float feedback = 0.55f + decay_norm_ * 0.44f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      if (!locked_)
      {
        left_[write_pos_] = live_left;
        right_[write_pos_] = live_right;
        write_pos_ = (write_pos_ + 1U) % kMaxBufSamples;
        if (captured_ < kMaxBufSamples)
          ++captured_;
        play_pos_ = static_cast<float>(write_pos_);
      }

      uint32_t read_a = static_cast<uint32_t>(play_pos_) % kMaxBufSamples;
      uint32_t read_b = (read_a + 1U) % kMaxBufSamples;
      const float frac = play_pos_ - static_cast<float>(static_cast<uint32_t>(play_pos_));
      float echo_left = left_[read_a] + (left_[read_b] - left_[read_a]) * frac;
      float echo_right = right_[read_a] + (right_[read_b] - right_[read_a]) * frac;

      if (locked_)
      {
        echo_left = echo_left * feedback + fade_left_ * (1.f - feedback);
        echo_right = echo_right * feedback + fade_right_ * (1.f - feedback);
        fade_left_ = echo_left;
        fade_right_ = echo_right;
        play_pos_ += rate;
        const float start = static_cast<float>(lock_start_);
        const float end = start + static_cast<float>(delay_samples);
        if (play_pos_ >= end)
          play_pos_ -= static_cast<float>(delay_samples);
        if (play_pos_ < start)
          play_pos_ += static_cast<float>(delay_samples);
      }
      else
      {
        fade_left_ = echo_left;
        fade_right_ = echo_right;
      }

      const float target_wet = locked_ ? 1.f : 0.f;
      wet_ += (target_wet - wet_) * wet_coeff;
      const float amount = wet_ * mix_;
      // Locked: dry stays (Send=0) and the frozen buffer sits on top.
      out[0] = live_left * (1.f - amount * 0.15f) + echo_left * amount;
      out[1] = live_right * (1.f - amount * 0.15f) + echo_right * amount;

      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  uint32_t delayLength() const
  {
    const float beat = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()));
    const float beats = 0.125f + size_norm_ * 1.875f;
    uint32_t samples = static_cast<uint32_t>(beat * beats);
    if (samples < 256U)
      samples = 256U;
    if (samples > kMaxBufSamples / 2U)
      samples = kMaxBufSamples / 2U;
    return samples;
  }

  float playbackRate() const
  {
    return 0.5f + pitch_norm_ * 1.0f;
  }

  void setLocked(bool next)
  {
    if (next == locked_)
      return;
    locked_ = next;
    if (!locked_)
      return;
    const uint32_t length = delayLength();
    lock_start_ = (write_pos_ + kMaxBufSamples - length) % kMaxBufSamples;
    play_pos_ = static_cast<float>(lock_start_);
  }

  float *left_ = nullptr;
  float *right_ = nullptr;
  uint32_t write_pos_ = 0U;
  uint32_t lock_start_ = 0U;
  uint32_t captured_ = 0U;
  float play_pos_ = 0.f;
  float bpm_ = 120.f;
  float size_norm_ = 0.5f;
  float pitch_norm_ = 0.5f;
  float decay_norm_ = 0.88f;
  float mix_ = 1.f;
  float wet_ = 0.f;
  float fade_left_ = 0.f;
  float fade_right_ = 0.f;
  bool locked_ = false;
  bool pad_held_ = false;
  bool toggle_mode_ = false;
};
