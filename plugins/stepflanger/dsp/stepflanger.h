#pragma once

/*
 * File: stepflanger.h
 *
 * Tempo-synced sample-and-hold flanger. Dry by default; touch engages.
 * Y is dual around center: |Y| = LFO depth, sign(Y) = feedback polarity.
 * STEPS redraws the random feedback amount on a grid.
 * LFO is a separate tempo-synced sweep cycle (not free Hz — avoids clashing
 * with STEPS). X = base delay TIME.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class StepFlanger : public Processor
{
public:
  static constexpr uint32_t kMaxDelaySamples = 2048U;
  static constexpr float kMinDelayMs = 0.35f;
  static constexpr float kMaxDelayMs = 12.f;
  static constexpr float kMaxModMs = 7.5f;
  static constexpr float kMaxFeedback = 0.92f;
  static constexpr float kParamSmoothCoeff = 0.0025f;
  static constexpr float kMinSlewSec = 0.0005f;
  static constexpr float kMaxSlewSec = 0.18f;
  static constexpr uint8_t kNumPeriods = 8U;
  static constexpr uint8_t kNumLfoCycles = 8U;

  uint32_t getBufferSize() const override final { return kMaxDelaySamples * 2U; }

  enum
  {
    TIME = 0U,
    DEPTH,
    MIX,
    STEPS,
    LFO,
    SLEW,
    NUM_PARAMS
  };

  enum
  {
    PERIOD_4BAR = 0U,
    PERIOD_2BAR,
    PERIOD_16STEP,
    PERIOD_8STEP,
    PERIOD_4STEP,
    PERIOD_2STEP,
    PERIOD_1STEP,
    PERIOD_HALF
  };

  enum
  {
    LFO_4BAR = 0U,
    LFO_2BAR,
    LFO_1BAR,
    LFO_HALF,
    LFO_QUARTER,
    LFO_8TH,
    LFO_16TH,
    LFO_32ND
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case TIME:
      time_norm_ = param_10bit_to_f32(value);
      break;
    case DEPTH:
      // Bipolar Y: center ≈ 0, up = +, down = −.
      // |value| drives LFO depth; sign drives feedback polarity.
      y_bipolar_target_ = (param_10bit_to_f32(value) - 0.5f) * 2.f;
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case STEPS:
      period_sel_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, static_cast<float>(kNumPeriods - 1U)));
      break;
    case LFO:
      lfo_sel_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, static_cast<float>(kNumLfoCycles - 1U)));
      break;
    case SLEW:
      slew_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    static const char *period_names[kNumPeriods] = {"4Bar", "2Bar", "16St", "8St", "4St", "2St", "1St", "1/2"};
    static const char *lfo_names[kNumLfoCycles] = {"4Bar", "2Bar", "1Bar", "1/2", "1/4", "1/8", "1/16", "1/32"};
    if (index == STEPS && value >= 0 && value < static_cast<int32_t>(kNumPeriods))
      return period_names[value];
    if (index == LFO && value >= 0 && value < static_cast<int32_t>(kNumLfoCycles))
      return lfo_names[value];
    return nullptr;
  }

  void init(float *allocated_buffer) override final
  {
    delay_left_ = allocated_buffer;
    delay_right_ = allocated_buffer != nullptr ? allocated_buffer + kMaxDelaySamples : nullptr;
    if (allocated_buffer != nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < kMaxDelaySamples * 2U; ++sampleIndex)
        allocated_buffer[sampleIndex] = 0.f;
    }

    bpm_ = 120.f;
    y_bipolar_target_ = 0.55f;
    y_bipolar_smooth_ = 0.55f;
    time_norm_ = 0.35f;
    slew_norm_ = 0.15f;
    mix_ = 1.f;
    period_sel_ = PERIOD_1STEP;
    lfo_sel_ = LFO_1BAR;
    clock_acc_ = 0.f;
    hold_amount_ = 0.65f;
    feed_smooth_ = 0.f;
    depth_smooth_ = 0.55f;
    lfo_phase_ = 0.f;
    write_pos_ = 0U;
    rng_ = 0xC0FFEE71U;
    pad_held_ = false;
    clearDelay();
  }

  void teardown() override final
  {
    delay_left_ = nullptr;
    delay_right_ = nullptr;
  }

  void reset() override final
  {
    y_bipolar_smooth_ = y_bipolar_target_;
    depth_smooth_ = fx::absf(y_bipolar_smooth_);
    clock_acc_ = 0.f;
    hold_amount_ = 0.65f;
    feed_smooth_ = 0.f;
    lfo_phase_ = 0.f;
    write_pos_ = 0U;
    pad_held_ = false;
    clearDelay();
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 40.f && tempo < 300.f)
      bpm_ = tempo;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began)
    {
      pad_held_ = true;
      clock_acc_ = 0.f;
      sampleHold();
      return;
    }
    if (phase == k_unit_touch_phase_moved || phase == k_unit_touch_phase_stationary)
    {
      pad_held_ = true;
      return;
    }
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      pad_held_ = false;
      clearDelay();
      feed_smooth_ = 0.f;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    if (delay_left_ == nullptr || delay_right_ == nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
      {
        out[0] = in[0];
        out[1] = in[1];
        in += 2;
        out += 2;
      }
      return;
    }

    const float sr = getSampleRate();
    const float beat = static_cast<float>(fx::samplesPerBeat(bpm_, sr));
    const float period_samples = beat * 0.25f * periodSixteenths(period_sel_);
    const float lfo_cycle_samples = beat * 0.25f * lfoSixteenths(lfo_sel_);
    const float lfo_inc = 1.f / fx::clip(lfo_cycle_samples, 1.f, sr * 60.f);

    const float slew_sec = kMinSlewSec + slew_norm_ * slew_norm_ * (kMaxSlewSec - kMinSlewSec);
    const float slew_x = -1.f / (slew_sec * sr);
    const float slew_coeff = fx::clip(1.f + slew_x, 0.f, 1.f);

    const float base_ms = kMinDelayMs + time_norm_ * time_norm_ * (kMaxDelayMs - kMinDelayMs);
    const float ms_to_samples = sr * 0.001f;
    const float delay_max = static_cast<float>(kMaxDelaySamples - 4U);

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      if (!pad_held_)
      {
        out[0] = live_left;
        out[1] = live_right;
        in += 2;
        if (raw != nullptr)
          raw += 2;
        out += 2;
        continue;
      }

      y_bipolar_smooth_ += (y_bipolar_target_ - y_bipolar_smooth_) * kParamSmoothCoeff;
      const float depth_target = fx::absf(y_bipolar_smooth_);
      depth_smooth_ += (depth_target - depth_smooth_) * kParamSmoothCoeff;

      clock_acc_ += 1.f;
      if (clock_acc_ >= period_samples)
      {
        clock_acc_ -= period_samples;
        sampleHold();
      }

      // Feedback amount is step-random; polarity and scale follow Y from center.
      const float feed_target =
          fx::clip(y_bipolar_smooth_ * hold_amount_ * kMaxFeedback, -kMaxFeedback, kMaxFeedback);
      feed_smooth_ += (feed_target - feed_smooth_) * slew_coeff;

      lfo_phase_ += lfo_inc;
      if (lfo_phase_ >= 1.f)
        lfo_phase_ -= 1.f;

      const float tri = (lfo_phase_ < 0.5f) ? (lfo_phase_ * 4.f - 1.f) : (3.f - lfo_phase_ * 4.f);
      const float mod_ms = depth_smooth_ * kMaxModMs * tri;

      float delay_l = (base_ms + mod_ms) * ms_to_samples;
      float delay_r = (base_ms + mod_ms * 0.92f + 0.35f) * ms_to_samples;
      delay_l = fx::clip(delay_l, 2.f, delay_max);
      delay_r = fx::clip(delay_r, 2.f, delay_max);

      const float delayed_left = readDelay(delay_left_, write_pos_, delay_l);
      const float delayed_right = readDelay(delay_right_, write_pos_, delay_r);

      float write_left = live_left + delayed_left * feed_smooth_;
      float write_right = live_right + delayed_right * feed_smooth_;
      write_left = fx::softclip(fx::clip(write_left, -1.5f, 1.5f));
      write_right = fx::softclip(fx::clip(write_right, -1.5f, 1.5f));

      delay_left_[write_pos_] = write_left;
      delay_right_[write_pos_] = write_right;
      ++write_pos_;
      if (write_pos_ >= kMaxDelaySamples)
        write_pos_ = 0U;

      const float wet_left = fx::softclip(fx::clip(delayed_left, -1.5f, 1.5f));
      const float wet_right = fx::softclip(fx::clip(delayed_right, -1.5f, 1.5f));

      out[0] = fx::mix(live_left, wet_left, mix_);
      out[1] = fx::mix(live_right, wet_right, mix_);
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  void clearDelay()
  {
    write_pos_ = 0U;
    if (delay_left_ == nullptr || delay_right_ == nullptr)
      return;
    for (uint32_t sampleIndex = 0; sampleIndex < kMaxDelaySamples; ++sampleIndex)
    {
      delay_left_[sampleIndex] = 0.f;
      delay_right_[sampleIndex] = 0.f;
    }
  }

  void sampleHold()
  {
    hold_amount_ = fx::randomFloat(rng_);
  }

  static float periodSixteenths(uint8_t period_sel)
  {
    static const float kPeriods[kNumPeriods] = {64.f, 32.f, 16.f, 8.f, 4.f, 2.f, 1.f, 0.5f};
    return kPeriods[period_sel < kNumPeriods ? period_sel : PERIOD_1STEP];
  }

  static float lfoSixteenths(uint8_t lfo_sel)
  {
    // One full triangle cycle as a note length (in 16ths).
    static const float kCycles[kNumLfoCycles] = {64.f, 32.f, 16.f, 8.f, 4.f, 2.f, 1.f, 0.5f};
    return kCycles[lfo_sel < kNumLfoCycles ? lfo_sel : LFO_1BAR];
  }

  static float readDelay(const float *buffer, uint32_t write_pos, float delay_samples)
  {
    float read_pos = static_cast<float>(write_pos) - delay_samples;
    while (read_pos < 0.f)
      read_pos += static_cast<float>(kMaxDelaySamples);
    const uint32_t index_a = static_cast<uint32_t>(read_pos);
    const float frac = read_pos - static_cast<float>(index_a);
    const uint32_t index_b = (index_a + 1U >= kMaxDelaySamples) ? 0U : index_a + 1U;
    return buffer[index_a] + (buffer[index_b] - buffer[index_a]) * frac;
  }

  float *delay_left_ = nullptr;
  float *delay_right_ = nullptr;
  uint32_t write_pos_ = 0U;

  float clock_acc_ = 0.f;
  float hold_amount_ = 0.65f;
  float feed_smooth_ = 0.f;
  float depth_smooth_ = 0.55f;
  float lfo_phase_ = 0.f;
  float bpm_ = 120.f;
  float y_bipolar_target_ = 0.55f;
  float y_bipolar_smooth_ = 0.55f;
  float time_norm_ = 0.35f;
  float slew_norm_ = 0.15f;
  float mix_ = 1.f;
  uint32_t rng_ = 0xC0FFEE71U;
  uint8_t period_sel_ = PERIOD_1STEP;
  uint8_t lfo_sel_ = LFO_1BAR;
  bool pad_held_ = false;
};
