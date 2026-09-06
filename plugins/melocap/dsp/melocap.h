#pragma once

/*
 * File: melocap.h
 *
 * DJM melodic capture. Always writes a short ring. Touch plays the frozen
 * grain at a scale-quantized rate; a long hold recaptures.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class MeloCap : public Processor
{
public:
  static constexpr uint32_t kMaxBuf = 48000U;

  uint32_t getBufferSize() const override final { return kMaxBuf * 2U; }

  enum
  {
    NOTE = 0U,
    SIZE,
    MIX,
    SCALE,
    ROOT,
    NUM_PARAMS
  };

  enum
  {
    SCALE_MIN = 0,
    SCALE_MAJ,
    SCALE_PENT
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case NOTE:
      note_norm_ = param_10bit_to_f32(value);
      break;
    case SIZE:
      size_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case SCALE:
      scale_sel_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, 2.f));
      break;
    case ROOT:
      root_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index != SCALE)
      return nullptr;
    if (value <= 0)
      return "MIN";
    if (value == 1)
      return "MAJ";
    return "PENT";
  }

  void init(float *allocated_buffer) override final
  {
    left_ = allocated_buffer;
    right_ = allocated_buffer + kMaxBuf;
    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;
    write_pos_ = 0U;
    captured_ = 0U;
    freeze_end_ = 0U;
    play_pos_ = 0.f;
    hold_samples_ = 0.f;
    playing_ = false;
    has_freeze_ = false;
    recaptured_ = false;
  }

  void teardown() override final
  {
    left_ = nullptr;
    right_ = nullptr;
  }

  void reset() override final
  {
    write_pos_ = 0U;
    captured_ = 0U;
    play_pos_ = 0.f;
    hold_samples_ = 0.f;
    has_freeze_ = false;
    recaptured_ = false;
    playing_ = false;
    if (left_ != nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < kMaxBuf; ++sampleIndex)
      {
        left_[sampleIndex] = 0.f;
        right_[sampleIndex] = 0.f;
      }
    }
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began)
    {
      playing_ = true;
      hold_samples_ = 0.f;
      recaptured_ = false;
      play_pos_ = 0.f;
      if (!has_freeze_)
        captureFreeze();
      return;
    }
    if (phase == k_unit_touch_phase_moved || phase == k_unit_touch_phase_stationary)
    {
      playing_ = true;
      return;
    }
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
      playing_ = false;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const uint32_t grain = grainSamples();
    const float grain_f = static_cast<float>(grain);
    const float increment = fasterpow2f(semitone() * (1.f / 12.f));
    const float hold_need = 0.25f * getSampleRate();
    const float xfade = 24.f + size_norm_ * 48.f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);
      left_[write_pos_] = live_left;
      right_[write_pos_] = live_right;
      write_pos_ = (write_pos_ + 1U) % kMaxBuf;
      if (captured_ < kMaxBuf)
        ++captured_;

      if (playing_)
      {
        hold_samples_ += 1.f;
        if (!recaptured_ && hold_samples_ > hold_need)
        {
          captureFreeze();
          recaptured_ = true;
          play_pos_ = 0.f;
        }
      }

      float wet_left = live_left;
      float wet_right = live_right;
      if (playing_ && has_freeze_ && grain > 8U)
      {
        readFreeze(play_pos_, grain, wet_left, wet_right);
        const float fade_in = (play_pos_ < xfade) ? (play_pos_ / xfade) : 1.f;
        const float tail = grain_f - play_pos_;
        const float fade_out = (tail < xfade) ? (tail / xfade) : 1.f;
        const float window = fade_in * fade_out;
        wet_left *= window;
        wet_right *= window;
        play_pos_ += increment;
        if (play_pos_ >= grain_f)
          play_pos_ -= grain_f;
        if (play_pos_ < 0.f)
          play_pos_ += grain_f;
      }

      const float amount = playing_ ? mix_ : 0.f;
      out[0] = fx::mix(live_left, wet_left, amount);
      out[1] = fx::mix(live_right, wet_right, amount);
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  uint32_t grainSamples() const
  {
    const float seconds = 0.04f + size_norm_ * 0.36f;
    uint32_t samples = static_cast<uint32_t>(seconds * getSampleRate());
    if (samples < 64U)
      samples = 64U;
    if (samples > kMaxBuf)
      samples = kMaxBuf;
    return samples;
  }

  uint32_t scaleLen() const
  {
    return (scale_sel_ == SCALE_MAJ) ? 7U : 5U;
  }

  const int8_t *scaleIntervals() const
  {
    static const int8_t kMin[] = {0, 3, 5, 7, 10};
    static const int8_t kMaj[] = {0, 2, 4, 5, 7, 9, 11};
    static const int8_t kPent[] = {0, 3, 5, 7, 10};
    if (scale_sel_ == SCALE_MAJ)
      return kMaj;
    if (scale_sel_ == SCALE_PENT)
      return kPent;
    return kMin;
  }

  float semitone() const
  {
    const uint32_t count = scaleLen();
    const uint32_t degrees = count * 2U;
    uint32_t degreeIndex = static_cast<uint32_t>(note_norm_ * static_cast<float>(degrees));
    if (degreeIndex >= degrees)
      degreeIndex = degrees - 1U;
    const int8_t *scale = scaleIntervals();
    const float octave = static_cast<float>(degreeIndex / count);
    const float interval = static_cast<float>(scale[degreeIndex % count]);
    return interval + octave * 12.f + root_norm_ * 12.f;
  }

  void captureFreeze()
  {
    freeze_end_ = write_pos_;
    has_freeze_ = captured_ > 64U;
  }

  void readFreeze(float pos, uint32_t grain, float &left, float &right) const
  {
    const float span = static_cast<float>(grain);
    while (pos < 0.f)
      pos += span;
    while (pos >= span)
      pos -= span;
    const uint32_t origin = (freeze_end_ + kMaxBuf - grain) % kMaxBuf;
    const uint32_t index_a = (origin + static_cast<uint32_t>(pos)) % kMaxBuf;
    const uint32_t index_b = (index_a + 1U) % kMaxBuf;
    const float frac = pos - static_cast<float>(static_cast<uint32_t>(pos));
    left = left_[index_a] + (left_[index_b] - left_[index_a]) * frac;
    right = right_[index_a] + (right_[index_b] - right_[index_a]) * frac;
  }

  float *left_ = nullptr;
  float *right_ = nullptr;
  uint32_t write_pos_ = 0U;
  uint32_t captured_ = 0U;
  uint32_t freeze_end_ = 0U;
  float play_pos_ = 0.f;
  float hold_samples_ = 0.f;
  float note_norm_ = 0.5f;
  float size_norm_ = 0.293f;
  float root_norm_ = 0.f;
  float mix_ = 1.f;
  uint8_t scale_sel_ = SCALE_MIN;
  bool playing_ = false;
  bool has_freeze_ = false;
  bool recaptured_ = false;
};
