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
    buffer_ = allocated_buffer;
    write_index_ = 0;
    clearBuffer();
    params_.reset();
    delay_samples_ = delaySamplesForTime(params_.time);
  }

  void teardown() override final { buffer_ = nullptr; }

  void reset() override final
  {
    clearBuffer();
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

      float delayed_l = 0.f;
      float delayed_r = 0.f;
      readFrac(delay_samples_, delayed_l, delayed_r);
      if (ping_pong)
      {
        const float crossed_l = delayed_r;
        delayed_r = delayed_l;
        delayed_l = crossed_l;
      }

      const float input_l = in[0];
      const float input_r = in[1];
      writeFrame(clipUnit(input_l + delayed_l * feedback), clipUnit(input_r + delayed_r * feedback));

      out[0] = input_l * dry_amt + delayed_l * wet_amt;
      out[1] = input_r * dry_amt + delayed_r * wet_amt;
      in += 2;
      out += 2;
    }
  }

private:
  static float clipUnit(float x)
  {
    if (x > 1.f)
      return 1.f;
    if (x < -1.f)
      return -1.f;
    return x;
  }

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

  void clearBuffer()
  {
    if (!buffer_)
      return;
    const uint32_t sample_count = getBufferSize();
    for (uint32_t sample_index = 0; sample_index < sample_count; ++sample_index)
      buffer_[sample_index] = 0.f;
  }

  // Same wrap as logue-sdk DelayLine: write index counts down, read is write + delay.
  void writeFrame(float left, float right)
  {
    const uint32_t frame = write_index_ & (kDelayFrames - 1);
    buffer_[frame * 2] = left;
    buffer_[frame * 2 + 1] = right;
    write_index_--;
  }

  void readFrac(float delay_samples, float &left, float &right) const
  {
    const uint32_t base = static_cast<uint32_t>(delay_samples);
    const float frac = delay_samples - static_cast<float>(base);
    const uint32_t mask = kDelayFrames - 1;
    const uint32_t index0 = (write_index_ + base) & mask;
    const uint32_t index1 = (write_index_ + base + 1) & mask;
    const float left0 = buffer_[index0 * 2];
    const float right0 = buffer_[index0 * 2 + 1];
    left = left0 + frac * (buffer_[index1 * 2] - left0);
    right = right0 + frac * (buffer_[index1 * 2 + 1] - right0);
  }

  float *buffer_ = nullptr;
  uint32_t write_index_ = 0;
  Params params_;
  float delay_samples_ = 1.f;
};
