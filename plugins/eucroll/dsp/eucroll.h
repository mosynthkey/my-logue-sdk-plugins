#pragma once

/*
 * File: eucroll.h
 *
 * Euclidean step roll. Always records AUDIO IN. Touch engages a tempo-synced
 * step roll only on Euclidean hit steps; non-hits stay dry. Y shortens the
 * loop inside a hit step. Depth (PAN) sets per-hit random stereo width.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class EucRoll : public Processor
{
public:
  static constexpr uint32_t kMaxBuf = 192000U;

  uint32_t getBufferSize() const override final { return kMaxBuf * 2U; }

  enum
  {
    DENS = 0U,
    ROLL,
    PAN,
    STEPS,
    ROT,
    GLUE,
    MIX,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case DENS:
      dens_norm_ = param_10bit_to_f32(value);
      break;
    case ROLL:
      roll_norm_ = param_10bit_to_f32(value);
      break;
    case PAN:
      pan_norm_ = fx::clip01(value / 1000.f);
      break;
    case STEPS:
      steps_sel_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, 2.f));
      break;
    case ROT:
      rot_norm_ = param_10bit_to_f32(value);
      break;
    case GLUE:
      glue_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index != STEPS)
      return nullptr;
    if (value <= 0)
      return "8";
    if (value == 1)
      return "12";
    return "16";
  }

  void init(float *allocated_buffer) override final
  {
    left_ = allocated_buffer;
    right_ = allocated_buffer + kMaxBuf;
    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;
    write_pos_ = 0U;
    captured_ = 0U;
    loop_start_ = 0U;
    loop_len_ = 2048U;
    loop_pos_ = 0.f;
    clock_acc_ = 0.f;
    step_index_ = 0U;
    rng_ = 17U;
    pan_left_ = 1.f;
    pan_right_ = 1.f;
    rolling_ = false;
    step_hit_ = false;
    has_loop_ = false;
    arm_current_ = false;
    bpm_ = 120.f;
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
    clock_acc_ = 0.f;
    step_index_ = 0U;
    rolling_ = false;
    step_hit_ = false;
    has_loop_ = false;
    arm_current_ = false;
    loop_pos_ = 0.f;
    pan_left_ = 1.f;
    pan_right_ = 1.f;
  }

  void setTempo(float tempo) override final
  {
    if (tempo >= 40.f && tempo <= 300.f)
      bpm_ = tempo;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began)
    {
      rolling_ = true;
      // If the current step is already a hit, start rolling immediately.
      arm_current_ = true;
      return;
    }
    if (phase == k_unit_touch_phase_moved || phase == k_unit_touch_phase_stationary)
    {
      rolling_ = true;
      return;
    }
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      rolling_ = false;
      step_hit_ = false;
      has_loop_ = false;
      arm_current_ = false;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const uint32_t steps = (steps_sel_ == 0) ? 8U : (steps_sel_ == 1 ? 12U : 16U);
    const uint32_t hits = 1U + static_cast<uint32_t>(dens_norm_ * static_cast<float>(steps - 1U));
    const uint32_t rotate = static_cast<uint32_t>(rot_norm_ * static_cast<float>(steps));
    const float beat = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()));
    const float step_samples = beat * 4.f / static_cast<float>(steps);
    const uint32_t roll_div = rollDivisions(roll_norm_);
    const float xfade = 16.f + glue_norm_ * 240.f;

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

      if (rolling_ && arm_current_)
      {
        const uint32_t rotated = (step_index_ + rotate) % steps;
        if (fx::euclidHit(rotated, hits, steps))
        {
          step_hit_ = true;
          captureStep(step_samples, roll_div);
        }
        else
        {
          step_hit_ = false;
          has_loop_ = false;
        }
        arm_current_ = false;
      }

      clock_acc_ += 1.f;
      if (clock_acc_ >= step_samples)
      {
        clock_acc_ -= step_samples;
        step_index_ = (step_index_ + 1U) % steps;
        const uint32_t rotated = (step_index_ + rotate) % steps;
        const bool hit = fx::euclidHit(rotated, hits, steps);
        if (rolling_ && hit)
        {
          step_hit_ = true;
          captureStep(step_samples, roll_div);
        }
        else
        {
          // Non-hits stay dry; do not continue a previous roll.
          step_hit_ = false;
          has_loop_ = false;
        }
      }

      // Live Y changes retune the loop length without recapturing origin.
      if (rolling_ && step_hit_ && has_loop_)
      {
        const uint32_t want = loopLengthFromStep(step_samples, roll_div);
        if (want != loop_len_ && want > 8U)
        {
          loop_len_ = want;
          if (loop_pos_ >= static_cast<float>(loop_len_))
            loop_pos_ = 0.f;
        }
      }

      float wet_left = live_left;
      float wet_right = live_right;
      const bool active = rolling_ && step_hit_ && has_loop_ && loop_len_ > 8U;
      if (active)
      {
        readLoop(loop_pos_, wet_left, wet_right);
        const float fade = (loop_pos_ < xfade) ? (loop_pos_ / xfade) : 1.f;
        const float tail = static_cast<float>(loop_len_) - loop_pos_;
        const float fade_out = (tail < xfade) ? (tail / xfade) : 1.f;
        const float window = fade * fade_out;
        wet_left *= window * pan_left_;
        wet_right *= window * pan_right_;
        loop_pos_ += 1.f;
        if (loop_pos_ >= static_cast<float>(loop_len_))
        {
          loop_pos_ -= static_cast<float>(loop_len_);
          // Fresh random pan each micro-roll repeat.
          rollPan();
        }
      }

      const float amount = active ? mix_ : 0.f;
      out[0] = fx::mix(live_left, wet_left, amount);
      out[1] = fx::mix(live_right, wet_right, amount);
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  static uint32_t rollDivisions(float norm)
  {
    // 1, 2, 4, 8 subdivisions of the current step.
    static const uint32_t kDiv[] = {1U, 2U, 4U, 8U};
    const float index = fx::clip01(norm) * 3.f;
    const uint32_t lower = static_cast<uint32_t>(index);
    const uint32_t upper = (lower < 3U) ? lower + 1U : 3U;
    const float frac = index - static_cast<float>(lower);
    if (frac < 0.5f)
      return kDiv[lower];
    return kDiv[upper];
  }

  static uint32_t loopLengthFromStep(float step_samples, uint32_t roll_div)
  {
    uint32_t samples = static_cast<uint32_t>(step_samples / static_cast<float>(roll_div));
    if (samples < 64U)
      samples = 64U;
    if (samples > kMaxBuf / 2U)
      samples = kMaxBuf / 2U;
    return samples;
  }

  void rollPan()
  {
    if (pan_norm_ <= 0.001f)
    {
      pan_left_ = 1.f;
      pan_right_ = 1.f;
      return;
    }
    // Equal-power pan from center toward a random side, scaled by Depth.
    const float side = fx::randomFloat(rng_) * 2.f - 1.f;
    const float amount = side * pan_norm_;
    const float angle = (amount + 1.f) * 0.7853981633974483f; // 0..pi/2
    pan_left_ = fastercosfullf(angle);
    pan_right_ = fastersinfullf(angle);
  }

  void captureStep(float step_samples, uint32_t roll_div)
  {
    loop_len_ = loopLengthFromStep(step_samples, roll_div);
    if (captured_ < loop_len_ + 8U)
      return;

    // Align to the current step's sync point so rolls restart on the grid.
    const uint32_t into_step = static_cast<uint32_t>(clock_acc_);
    const uint32_t step_len = static_cast<uint32_t>(step_samples);
    uint32_t origin = loop_len_;
    if (into_step >= loop_len_)
      origin = into_step;
    else if (captured_ >= step_len)
      origin = step_len;

    if (captured_ < origin)
      origin = captured_;
    if (origin < loop_len_)
      origin = loop_len_;

    loop_start_ = (write_pos_ + kMaxBuf - origin) % kMaxBuf;
    loop_pos_ = 0.f;
    has_loop_ = true;
    rollPan();
  }

  void readLoop(float pos, float &left, float &right) const
  {
    const uint32_t index_a = (loop_start_ + static_cast<uint32_t>(pos)) % kMaxBuf;
    const uint32_t index_b = (index_a + 1U) % kMaxBuf;
    const float frac = pos - static_cast<float>(static_cast<uint32_t>(pos));
    left = left_[index_a] + (left_[index_b] - left_[index_a]) * frac;
    right = right_[index_a] + (right_[index_b] - right_[index_a]) * frac;
  }

  float *left_ = nullptr;
  float *right_ = nullptr;
  uint32_t write_pos_ = 0U;
  uint32_t captured_ = 0U;
  uint32_t loop_start_ = 0U;
  uint32_t loop_len_ = 2048U;
  uint32_t step_index_ = 0U;
  uint32_t rng_ = 17U;
  float loop_pos_ = 0.f;
  float clock_acc_ = 0.f;
  float bpm_ = 120.f;
  float dens_norm_ = 0.4f;
  float roll_norm_ = 0.45f;
  float rot_norm_ = 0.f;
  float glue_norm_ = 0.18f;
  float pan_norm_ = 0.55f;
  float mix_ = 1.f;
  float pan_left_ = 1.f;
  float pan_right_ = 1.f;
  uint8_t steps_sel_ = 2;
  bool rolling_ = false;
  bool step_hit_ = false;
  bool has_loop_ = false;
  bool arm_current_ = false;
};
