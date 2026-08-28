#pragma once

/*
 * File: delay.h
 *
 * Stereo delay with optional ping-pong. Used as delfx on NTS-1 mkII
 * and as genericfx on NTS-3.
 *
 */

#include "processor.h"
#include "macros.h"
#include "dsp/delayline.hpp"
#include "utils/float_math.h"

class Delay : public Processor
{
public:
  static constexpr uint32_t kDelayFrames = 65536;
  static constexpr float kMinDelaySec = 0.005f;
  static constexpr float kMaxDelaySec = 1.f;
  static constexpr float kMaxFeedback = 0.92f;

  uint32_t getBufferSize() const override final { return kDelayFrames * 2; }

  enum
  {
    TIME = 0U,
    FEED,
    MIX,
    MODE,
    NUM_PARAMS
  };

  enum
  {
    MODE_STEREO = 0,
    MODE_PING,
    NUM_MODES
  };

  struct Params
  {
    float time;
    float feedback;
    float mix;
    uint32_t mode;

    void reset()
    {
      time = 0.25f;
      feedback = 0.35f;
      mix = 0.35f;
      mode = MODE_STEREO;
    }

    Params() { reset(); }
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case TIME:
      params_.time = param_10bit_to_f32(value);
      break;
    case FEED:
      params_.feedback = param_10bit_to_f32(value);
      break;
    case MIX:
      params_.mix = value / 1000.f;
      break;
    case MODE:
      params_.mode = static_cast<uint32_t>(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    static const char *mode_strings[NUM_MODES] = {
        "STEREO",
        "PING",
    };

    if (index == MODE && value >= MODE_STEREO && value < NUM_MODES)
      return mode_strings[value];

    return nullptr;
  }

  void init(float *allocated_buffer) override final
  {
    delay_line_.setMemory(reinterpret_cast<f32pair_t *>(allocated_buffer), kDelayFrames);
    delay_line_.clear();
    params_.reset();
    delay_samples_ = delaySamplesForTime(params_.time);
  }

  void teardown() override final {}

  void reset() override final
  {
    delay_line_.clear();
    delay_samples_ = delaySamplesForTime(params_.time);
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    const Params p = params_;
    const float target_delay = delaySamplesForTime(p.time);
    const float feedback = p.feedback * kMaxFeedback;
    const float wet_amt = p.mix;
    const float dry_amt = 1.f - wet_amt;
    const bool ping_pong = p.mode == MODE_PING;

    for (uint32_t frame_index = 0; frame_index < frames; ++frame_index)
    {
      delay_samples_ += 0.0004f * (target_delay - delay_samples_);

      const f32pair_t tap = delay_line_.readFrac(delay_samples_);
      const float delayed_l = ping_pong ? tap.b : tap.a;
      const float delayed_r = ping_pong ? tap.a : tap.b;

      const float input_l = in[0];
      const float input_r = in[1];

      f32pair_t written;
      written.a = clip1m1f(input_l + delayed_l * feedback);
      written.b = clip1m1f(input_r + delayed_r * feedback);
      delay_line_.write(written);

      out[0] = input_l * dry_amt + delayed_l * wet_amt;
      out[1] = input_r * dry_amt + delayed_r * wet_amt;
      in += 2;
      out += 2;
    }
  }

private:
  static float delaySamplesForTime(float time_norm)
  {
    const float seconds = kMinDelaySec + time_norm * (kMaxDelaySec - kMinDelaySec);
    const float samples = seconds * getSampleRate();
    const float max_read = static_cast<float>(kDelayFrames - 4);
    if (samples < 1.f)
      return 1.f;
    if (samples > max_read)
      return max_read;
    return samples;
  }

  dsp::DualDelayLine delay_line_;
  Params params_;
  float delay_samples_ = 1.f;
};
