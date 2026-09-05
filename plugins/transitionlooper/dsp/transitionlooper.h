#pragma once

/*
 * File: transitionlooper.h
 *
 * Tempo-synced 16-step DJ transition looper for NTS-3. Always records the
 * last bar plus wrap-glue. Pad up bypasses; pad down fades into the frozen
 * loop using volume, filter, bass-swap, echo, brake, or roll transitions.
 * Playback is a direct loop read (no time-stretch) to stay light on the M7.
 */

#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class TransitionLooper : public Processor
{
public:
  static constexpr uint32_t kMaxLoopSamples = 288000U;
  static constexpr uint32_t kMaxEchoSamples = 48000U;
  static constexpr uint32_t kStepsPerBar = 16U;
  static constexpr uint32_t kMinLoopSamples = 1024U;
  static constexpr uint32_t kMinGlueSamples = 64U;
  static constexpr float kMinBpm = 40.f;
  static constexpr float kMaxBpm = 300.f;
  static constexpr float kTwoPi = 6.283185307179586f;

  uint32_t getBufferSize() const override final
  {
    return kMaxLoopSamples * 2U + kMaxEchoSamples * 2U;
  }

  enum
  {
    TIME = 0U,
    TONE,
    MIX,
    TYPE,
    GLUE,
    SYNC,
    NUM_PARAMS
  };

  enum
  {
    TYPE_VOL = 0,
    TYPE_HPF,
    TYPE_LPF,
    TYPE_BASS,
    TYPE_ECHO,
    TYPE_BRK,
    TYPE_ROLL,
    NUM_TYPES
  };

  enum
  {
    SYNC_16 = 0,
    SYNC_8,
    SYNC_4,
    SYNC_2,
    NUM_SYNCS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case TIME:
      time_norm_ = param_10bit_to_f32(value);
      updateFadeIncrement();
      break;
    case TONE:
      tone_norm_ = param_10bit_to_f32(value);
      filter_dirty_ = true;
      break;
    case MIX:
      mix_ = value / 1000.f;
      if (mix_ < 0.f)
        mix_ = 0.f;
      if (mix_ > 1.f)
        mix_ = 1.f;
      break;
    case TYPE:
    {
      int32_t type = value;
      if (type < 0)
        type = 0;
      if (type >= NUM_TYPES)
        type = NUM_TYPES - 1;
      if (type_ != static_cast<uint8_t>(type))
      {
        type_ = static_cast<uint8_t>(type);
        filter_dirty_ = true;
      }
      break;
    }
    case GLUE:
      glue_norm_ = param_10bit_to_f32(value);
      if (!frozen_)
        updateLoopGeometry();
      break;
    case SYNC:
    {
      int32_t sync = value;
      if (sync < 0)
        sync = 0;
      if (sync >= NUM_SYNCS)
        sync = NUM_SYNCS - 1;
      sync_ = static_cast<uint8_t>(sync);
      break;
    }
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    static const char *type_names[NUM_TYPES] = {"VOL", "HPF", "LPF", "BASS", "ECHO", "BRK", "ROLL"};
    static const char *sync_names[NUM_SYNCS] = {"1/16", "1/8", "1/4", "1/2"};

    if (index == TYPE && value >= 0 && value < NUM_TYPES)
      return type_names[value];
    if (index == SYNC && value >= 0 && value < NUM_SYNCS)
      return sync_names[value];
    return nullptr;
  }

  void init(float *allocated_buffer) override final
  {
    loop_left_ = allocated_buffer;
    loop_right_ = allocated_buffer + kMaxLoopSamples;
    echo_left_ = allocated_buffer + kMaxLoopSamples * 2U;
    echo_right_ = allocated_buffer + kMaxLoopSamples * 2U + kMaxEchoSamples;

    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;

    time_norm_ = 0.4f;
    mix_ = 1.f;
    type_ = TYPE_VOL;
    glue_norm_ = 0.39f;
    sync_ = SYNC_4;
    tone_norm_ = 0.68f;
    bpm_ = 120.f;
    updateLoopGeometry();
    updateFadeIncrement();
    reset();
  }

  void teardown() override final
  {
    loop_left_ = nullptr;
    loop_right_ = nullptr;
    echo_left_ = nullptr;
    echo_right_ = nullptr;
  }

  void reset() override final
  {
    write_pos_ = 0U;
    echo_pos_ = 0U;
    captured_samples_ = 0U;
    frozen_ = false;
    pad_held_ = false;
    wet_ = 0.f;
    wet_target_ = 0.f;
    play_pos_ = 0.f;
    play_window_ = 0U;
    live_lp_left_ = 0.f;
    live_lp_right_ = 0.f;
    loop_lp_left_ = 0.f;
    loop_lp_right_ = 0.f;
    bass_lp_live_left_ = 0.f;
    bass_lp_live_right_ = 0.f;
    bass_lp_loop_left_ = 0.f;
    bass_lp_loop_right_ = 0.f;
    filter_dirty_ = true;
    live_coeff_ = 0.05f;
    loop_coeff_ = 0.05f;
    bass_coeff_ = 0.023f;

    if (loop_left_ != nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < kMaxLoopSamples; ++sampleIndex)
      {
        loop_left_[sampleIndex] = 0.f;
        loop_right_[sampleIndex] = 0.f;
      }
    }
    if (echo_left_ != nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < kMaxEchoSamples; ++sampleIndex)
      {
        echo_left_[sampleIndex] = 0.f;
        echo_right_[sampleIndex] = 0.f;
      }
    }
  }

  void setTempo(float tempo) override final
  {
    if (tempo >= kMinBpm && tempo <= kMaxBpm)
    {
      bpm_ = tempo;
      updateFadeIncrement();
      if (!frozen_)
        updateLoopGeometry();
    }
  }

  void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    (void)id;
    (void)x;
    (void)y;

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      pad_held_ = false;
      wet_target_ = 0.f;
      return;
    }

    if (phase != k_unit_touch_phase_began && phase != k_unit_touch_phase_moved &&
        phase != k_unit_touch_phase_stationary)
      return;

    if (pad_held_)
      return;

    if (captured_samples_ < kMinLoopSamples)
      return;

    pad_held_ = true;
    freezeLoop();
    wet_target_ = 1.f;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      const float dry_left = in[0];
      const float dry_right = in[1];

      if (!frozen_)
        recordSample(dry_left, dry_right);

      advanceWet();

      if (wet_ <= 0.f)
      {
        out[0] = dry_left;
        out[1] = dry_right;
        in += 2;
        out += 2;
        continue;
      }

      float loop_left = 0.f;
      float loop_right = 0.f;
      if (frozen_)
        renderLoop(loop_left, loop_right);

      float live_left = dry_left;
      float live_right = dry_right;
      applyTransition(live_left, live_right, loop_left, loop_right);

      out[0] = live_left + loop_left * mix_;
      out[1] = live_right + loop_right * mix_;

      in += 2;
      out += 2;
    }
  }

  bool isFrozen() const { return frozen_; }
  float wetAmount() const { return wet_; }
  uint32_t loopLength() const { return loop_length_; }
  uint32_t capturedSamples() const { return captured_samples_; }

private:
  static float clamp01(float value)
  {
    if (value < 0.f)
      return 0.f;
    if (value > 1.f)
      return 1.f;
    return value;
  }

  static uint32_t wrapAdd(uint32_t index, uint32_t length)
  {
    if (length == 0U)
      return 0U;
    return index >= length ? index - length : index;
  }

  void updateFadeIncrement()
  {
    const float min_beats = 0.125f;
    const float max_beats = 8.f;
    const float beats = min_beats * fasterpowf(max_beats / min_beats, time_norm_);
    float seconds = beats * 60.f / bpm_;
    if (seconds < 0.01f)
      seconds = 0.01f;
    fade_increment_ = 1.f / (seconds * getSampleRate());
    if (fade_increment_ > 1.f)
      fade_increment_ = 1.f;
  }

  void updateLoopGeometry()
  {
    const float seconds_per_bar = 240.f / bpm_;
    uint32_t samples = static_cast<uint32_t>(seconds_per_bar * getSampleRate() + 0.5f);
    if (samples < kMinLoopSamples)
      samples = kMinLoopSamples;

    const uint32_t step_samples = samples / kStepsPerBar;
    uint32_t glue = kMinGlueSamples + static_cast<uint32_t>(glue_norm_ * static_cast<float>(step_samples * 2U));
    if (glue < kMinGlueSamples)
      glue = kMinGlueSamples;

    if (samples + glue > kMaxLoopSamples)
    {
      if (samples >= kMaxLoopSamples)
      {
        samples = kMaxLoopSamples;
        glue = kMinGlueSamples;
      }
      else
      {
        glue = kMaxLoopSamples - samples;
        if (glue < kMinGlueSamples)
          glue = kMinGlueSamples;
      }
    }

    loop_length_ = samples;
    glue_length_ = glue;
    record_length_ = samples + glue;
    if (record_length_ > kMaxLoopSamples)
      record_length_ = kMaxLoopSamples;
  }

  void freezeLoop()
  {
    updateLoopGeometry();
    frozen_ = true;
    play_window_ = loop_length_;
    if (captured_samples_ < loop_length_)
      play_window_ = captured_samples_;
    if (play_window_ < kMinLoopSamples)
      play_window_ = kMinLoopSamples;

    const uint32_t newest_index = write_pos_ == 0U ? record_length_ - 1U : write_pos_ - 1U;
    int32_t origin = static_cast<int32_t>(newest_index) - static_cast<int32_t>(play_window_) + 1;
    if (origin < 0)
      origin += static_cast<int32_t>(record_length_);
    loop_origin_ = static_cast<uint32_t>(origin);

    const uint32_t phase_in_bar = loop_length_ == 0U ? 0U : (captured_samples_ % loop_length_);
    play_pos_ = static_cast<float>(phase_in_bar);
    if (play_pos_ >= static_cast<float>(play_window_))
      play_pos_ = 0.f;

    loop_lp_left_ = 0.f;
    loop_lp_right_ = 0.f;
    filter_dirty_ = true;
  }

  void recordSample(float left, float right)
  {
    if (loop_left_ == nullptr || record_length_ == 0U)
      return;

    loop_left_[write_pos_] = left;
    loop_right_[write_pos_] = right;
    ++write_pos_;
    if (write_pos_ >= record_length_)
      write_pos_ = 0U;
    if (captured_samples_ < record_length_)
      ++captured_samples_;
  }

  void advanceWet()
  {
    if (wet_ < wet_target_)
    {
      wet_ += fade_increment_;
      if (wet_ > wet_target_)
        wet_ = wet_target_;
      filter_dirty_ = true;
    }
    else if (wet_ > wet_target_)
    {
      wet_ -= fade_increment_;
      if (wet_ < wet_target_)
        wet_ = wet_target_;
      filter_dirty_ = true;
    }

    if (!pad_held_ && wet_ <= 0.f && frozen_)
    {
      frozen_ = false;
      updateLoopGeometry();
    }
  }

  void updatePlayWindow()
  {
    play_window_ = loop_length_;
    if (play_window_ > captured_samples_ && captured_samples_ >= kMinLoopSamples)
      play_window_ = captured_samples_;

    if (type_ != TYPE_ROLL)
      return;

    const float roll_amount = wet_target_ >= 1.f ? wet_ : (1.f - wet_);
    uint32_t shifts = static_cast<uint32_t>(roll_amount * 4.f);
    if (sync_ == SYNC_16)
      ++shifts;
    else if (sync_ == SYNC_2 && shifts > 0U)
      --shifts;

    uint32_t window = loop_length_ >> shifts;
    const uint32_t min_window = loop_length_ / kStepsPerBar;
    if (window < min_window)
      window = min_window;
    if (window < kMinLoopSamples)
      window = kMinLoopSamples;
    play_window_ = window;
  }

  void readLoopLinear(float play_pos, float &left, float &right) const
  {
    const uint32_t window = play_window_ == 0U ? 1U : play_window_;
    float pos = play_pos;
    while (pos >= static_cast<float>(window))
      pos -= static_cast<float>(window);
    while (pos < 0.f)
      pos += static_cast<float>(window);

    const uint32_t index_a = static_cast<uint32_t>(pos);
    const float frac = pos - static_cast<float>(index_a);
    const uint32_t index_b = wrapAdd(index_a + 1U, window);
    const uint32_t abs_a = wrapAdd(loop_origin_ + index_a, record_length_);
    const uint32_t abs_b = wrapAdd(loop_origin_ + index_b, record_length_);

    left = loop_left_[abs_a] + (loop_left_[abs_b] - loop_left_[abs_a]) * frac;
    right = loop_right_[abs_a] + (loop_right_[abs_b] - loop_right_[abs_a]) * frac;

    if (glue_length_ >= 8U && index_a + glue_length_ >= window)
    {
      const float fade = (pos - static_cast<float>(window - glue_length_)) / static_cast<float>(glue_length_);
      const float start_pos = pos - static_cast<float>(window) + static_cast<float>(glue_length_);
      float start_left = 0.f;
      float start_right = 0.f;
      const uint32_t start_index = static_cast<uint32_t>(start_pos);
      const float start_frac = start_pos - static_cast<float>(start_index);
      const uint32_t start_next = wrapAdd(start_index + 1U, window);
      const uint32_t start_abs = wrapAdd(loop_origin_ + start_index, record_length_);
      const uint32_t start_abs_next = wrapAdd(loop_origin_ + start_next, record_length_);
      start_left = loop_left_[start_abs] + (loop_left_[start_abs_next] - loop_left_[start_abs]) * start_frac;
      start_right = loop_right_[start_abs] + (loop_right_[start_abs_next] - loop_right_[start_abs]) * start_frac;
      const float out_gain = 1.f - fade;
      left = left * out_gain + start_left * fade;
      right = right * out_gain + start_right * fade;
    }
  }

  void renderLoop(float &left, float &right)
  {
    updatePlayWindow();
    readLoopLinear(play_pos_, left, right);

    float rate = 1.f;
    if (type_ == TYPE_BRK)
      rate = 0.4f + 0.6f * wet_;

    play_pos_ += rate;
    const float window = static_cast<float>(play_window_ == 0U ? 1U : play_window_);
    if (play_pos_ >= window)
      play_pos_ -= window;
  }

  void applyOnePole(float &state, float input, float coeff) const
  {
    state += coeff * (input - state);
  }

  float filterCoeffFromHz(float hz) const
  {
    float coeff = kTwoPi * hz / getSampleRate();
    if (coeff < 0.00005f)
      coeff = 0.00005f;
    if (coeff > 0.95f)
      coeff = 0.95f;
    return coeff;
  }

  float expLerpHz(float min_hz, float max_hz, float amount) const
  {
    return min_hz * fasterpowf(max_hz / min_hz, clamp01(amount));
  }

  void refreshFilterCoeffs()
  {
    if (!filter_dirty_)
      return;
    filter_dirty_ = false;

    const float amount = wet_;
    const float tone = 0.25f + tone_norm_ * 0.75f;
    if (type_ == TYPE_HPF)
    {
      const float hpf_max = expLerpHz(400.f, 6500.f, tone);
      live_coeff_ = filterCoeffFromHz(expLerpHz(22.f, hpf_max, amount));
      loop_coeff_ = filterCoeffFromHz(expLerpHz(hpf_max, 22.f, amount));
    }
    else if (type_ == TYPE_LPF)
    {
      const float lpf_min = expLerpHz(400.f, 180.f, tone);
      live_coeff_ = filterCoeffFromHz(expLerpHz(12000.f, lpf_min, amount));
      loop_coeff_ = filterCoeffFromHz(expLerpHz(lpf_min, 12000.f, amount));
    }
    else if (type_ == TYPE_BASS)
    {
      bass_coeff_ = filterCoeffFromHz(180.f);
    }
  }

  void applyEcho(float &left, float &right, float send_amount)
  {
    static const float kBeats[NUM_SYNCS] = {0.25f, 0.5f, 1.f, 2.f};
    float delay_samples = kBeats[sync_] * 60.f / bpm_ * getSampleRate();
    if (delay_samples < 64.f)
      delay_samples = 64.f;
    if (delay_samples > static_cast<float>(kMaxEchoSamples - 4U))
      delay_samples = static_cast<float>(kMaxEchoSamples - 4U);

    const uint32_t delay_int = static_cast<uint32_t>(delay_samples);
    int32_t read_index = static_cast<int32_t>(echo_pos_) - static_cast<int32_t>(delay_int);
    if (read_index < 0)
      read_index += static_cast<int32_t>(kMaxEchoSamples);
    const uint32_t read_a = static_cast<uint32_t>(read_index);
    const uint32_t read_b = wrapAdd(read_a + 1U, kMaxEchoSamples);
    const float delayed_left = echo_left_[read_a];
    const float delayed_right = echo_right_[read_a];
    (void)read_b;

    const float feedback = 0.42f + send_amount * 0.28f;
    echo_left_[echo_pos_] = left * send_amount + delayed_left * feedback;
    echo_right_[echo_pos_] = right * send_amount + delayed_right * feedback;
    ++echo_pos_;
    if (echo_pos_ >= kMaxEchoSamples)
      echo_pos_ = 0U;

    left += delayed_left * send_amount;
    right += delayed_right * send_amount;
  }

  void applyTransition(float &live_left, float &live_right, float &loop_left, float &loop_right)
  {
    const float amount = wet_;
    const float live_gain = 1.f - amount;
    const float loop_gain = amount;

    refreshFilterCoeffs();

    switch (type_)
    {
    case TYPE_HPF:
    {
      applyOnePole(live_lp_left_, live_left, live_coeff_);
      applyOnePole(live_lp_right_, live_right, live_coeff_);
      applyOnePole(loop_lp_left_, loop_left, loop_coeff_);
      applyOnePole(loop_lp_right_, loop_right, loop_coeff_);
      const float live_end = amount > 0.8f ? 1.f - (amount - 0.8f) * 5.f : 1.f;
      const float loop_start = amount < 0.2f ? amount * 5.f : 1.f;
      live_left = (live_left - live_lp_left_) * live_end;
      live_right = (live_right - live_lp_right_) * live_end;
      loop_left = (loop_left - loop_lp_left_) * loop_start;
      loop_right = (loop_right - loop_lp_right_) * loop_start;
      break;
    }

    case TYPE_LPF:
    {
      applyOnePole(live_lp_left_, live_left, live_coeff_);
      applyOnePole(live_lp_right_, live_right, live_coeff_);
      applyOnePole(loop_lp_left_, loop_left, loop_coeff_);
      applyOnePole(loop_lp_right_, loop_right, loop_coeff_);
      live_left = live_lp_left_ * live_gain;
      live_right = live_lp_right_ * live_gain;
      loop_left = loop_lp_left_ * loop_gain;
      loop_right = loop_lp_right_ * loop_gain;
      break;
    }

    case TYPE_BASS:
    {
      applyOnePole(bass_lp_live_left_, live_left, bass_coeff_);
      applyOnePole(bass_lp_live_right_, live_right, bass_coeff_);
      applyOnePole(bass_lp_loop_left_, loop_left, bass_coeff_);
      applyOnePole(bass_lp_loop_right_, loop_right, bass_coeff_);
      const float live_high_left = live_left - bass_lp_live_left_;
      const float live_high_right = live_right - bass_lp_live_right_;
      const float loop_high_left = loop_left - bass_lp_loop_left_;
      const float loop_high_right = loop_right - bass_lp_loop_right_;
      const float bass_swap = amount < 0.5f ? 0.f : clamp01((amount - 0.5f) * 8.f);
      live_left = live_high_left * live_gain + bass_lp_live_left_ * (1.f - bass_swap);
      live_right = live_high_right * live_gain + bass_lp_live_right_ * (1.f - bass_swap);
      loop_left = loop_high_left * loop_gain + bass_lp_loop_left_ * bass_swap;
      loop_right = loop_high_right * loop_gain + bass_lp_loop_right_ * bass_swap;
      break;
    }

    case TYPE_ECHO:
    {
      applyEcho(live_left, live_right, amount);
      live_left *= live_gain;
      live_right *= live_gain;
      loop_left *= loop_gain;
      loop_right *= loop_gain;
      break;
    }

    case TYPE_VOL:
    case TYPE_BRK:
    case TYPE_ROLL:
    default:
      live_left *= live_gain;
      live_right *= live_gain;
      loop_left *= loop_gain;
      loop_right *= loop_gain;
      break;
    }
  }

  float *loop_left_ = nullptr;
  float *loop_right_ = nullptr;
  float *echo_left_ = nullptr;
  float *echo_right_ = nullptr;

  float time_norm_ = 0.4f;
  float mix_ = 1.f;
  float glue_norm_ = 0.39f;
  float tone_norm_ = 0.68f;
  float bpm_ = 120.f;
  float wet_ = 0.f;
  float wet_target_ = 0.f;
  float fade_increment_ = 0.0001f;
  float play_pos_ = 0.f;
  float live_lp_left_ = 0.f;
  float live_lp_right_ = 0.f;
  float loop_lp_left_ = 0.f;
  float loop_lp_right_ = 0.f;
  float bass_lp_live_left_ = 0.f;
  float bass_lp_live_right_ = 0.f;
  float bass_lp_loop_left_ = 0.f;
  float bass_lp_loop_right_ = 0.f;
  float live_coeff_ = 0.05f;
  float loop_coeff_ = 0.05f;
  float bass_coeff_ = 0.023f;

  uint32_t loop_length_ = 96000U;
  uint32_t glue_length_ = 2048U;
  uint32_t record_length_ = 98048U;
  uint32_t play_window_ = 96000U;
  uint32_t loop_origin_ = 0U;
  uint32_t write_pos_ = 0U;
  uint32_t echo_pos_ = 0U;
  uint32_t captured_samples_ = 0U;
  uint8_t type_ = TYPE_VOL;
  uint8_t sync_ = SYNC_4;
  bool frozen_ = false;
  bool pad_held_ = false;
  bool filter_dirty_ = true;
};
