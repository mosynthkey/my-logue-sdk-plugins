#pragma once

/*
 * File: transitionlooper.h
 *
 * Tempo-synced 16-step DJ transition looper for NTS-3. Always records the
 * last bar plus wrap-glue. Pad up bypasses; pad down fades into the frozen
 * loop using volume, filter, bass-swap, echo, brake, or roll transitions.
 * Y drives a compact WSOLA time-stretch engine (pitch held).
 */

#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <math.h>
#include <stdint.h>

class TransitionLooper : public Processor
{
public:
  static constexpr uint32_t kMaxLoopSamples = 288000U;
  static constexpr uint32_t kMaxEchoSamples = 48000U;
  static constexpr uint32_t kStepsPerBar = 16U;
  static constexpr uint32_t kGrainCount = 2U;
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
    STRCH,
    MIX,
    TYPE,
    GLUE,
    SIZE,
    SYNC,
    TONE,
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
      break;
    case STRCH:
      stretch_target_ = stretchFromParam(value);
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
      type_ = static_cast<uint8_t>(type);
      break;
    }
    case GLUE:
      glue_norm_ = param_10bit_to_f32(value);
      break;
    case SIZE:
      size_norm_ = param_10bit_to_f32(value);
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
    case TONE:
      tone_norm_ = param_10bit_to_f32(value);
      break;
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
    stretch_target_ = 1.f;
    mix_ = 1.f;
    type_ = TYPE_HPF;
    glue_norm_ = 0.39f;
    size_norm_ = 0.5f;
    sync_ = SYNC_4;
    tone_norm_ = 0.68f;
    bpm_ = 120.f;
    updateLoopGeometry();
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
    analysis_pos_ = 0.f;
    play_window_ = 0U;
    stretch_smooth_ = 1.f;
    live_lp_left_ = 0.f;
    live_lp_right_ = 0.f;
    live_lp2_left_ = 0.f;
    live_lp2_right_ = 0.f;
    loop_lp_left_ = 0.f;
    loop_lp_right_ = 0.f;
    loop_lp2_left_ = 0.f;
    loop_lp2_right_ = 0.f;
    bass_lp_live_left_ = 0.f;
    bass_lp_live_right_ = 0.f;
    bass_lp2_live_left_ = 0.f;
    bass_lp2_live_right_ = 0.f;
    bass_lp_loop_left_ = 0.f;
    bass_lp_loop_right_ = 0.f;
    bass_lp2_loop_left_ = 0.f;
    bass_lp2_loop_right_ = 0.f;
    resetGrains();

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

      float loop_left = 0.f;
      float loop_right = 0.f;
      if (wet_ > 0.f && frozen_)
        renderLoop(loop_left, loop_right);

      float live_left = dry_left;
      float live_right = dry_right;
      applyTransition(live_left, live_right, loop_left, loop_right);

      const float loop_gain = mix_;
      out[0] = live_left + loop_left * loop_gain;
      out[1] = live_right + loop_right * loop_gain;

      in += 2;
      out += 2;
    }
  }

  bool isFrozen() const { return frozen_; }
  float wetAmount() const { return wet_; }
  uint32_t loopLength() const { return loop_length_; }
  uint32_t capturedSamples() const { return captured_samples_; }

private:
  struct Grain
  {
    bool active;
    float src_pos;
    uint32_t age;
    uint32_t length;
  };

  static float clamp01(float value)
  {
    if (value < 0.f)
      return 0.f;
    if (value > 1.f)
      return 1.f;
    return value;
  }

  static float hermite(float y0, float y1, float y2, float y3, float frac)
  {
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + y1;
  }

  static float equalPowerIn(float amount)
  {
    return sinf(clamp01(amount) * 1.57079632679f);
  }

  static float equalPowerOut(float amount)
  {
    return cosf(clamp01(amount) * 1.57079632679f);
  }

  static float expLerpHz(float min_hz, float max_hz, float amount)
  {
    const float clamped = clamp01(amount);
    return min_hz * powf(max_hz / min_hz, clamped);
  }

  static float stretchFromParam(int32_t value)
  {
    const float norm = param_10bit_to_f32(value);
    if (norm <= 0.5f)
      return 0.5f + norm;
    return 1.f + (norm - 0.5f) * 2.f;
  }

  static uint32_t wrapU32(int32_t index, uint32_t length)
  {
    if (length == 0U)
      return 0U;
    int32_t wrapped = index % static_cast<int32_t>(length);
    if (wrapped < 0)
      wrapped += static_cast<int32_t>(length);
    return static_cast<uint32_t>(wrapped);
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

  void resetGrains()
  {
    for (uint32_t grainIndex = 0; grainIndex < kGrainCount; ++grainIndex)
    {
      grains_[grainIndex].active = false;
      grains_[grainIndex].src_pos = 0.f;
      grains_[grainIndex].age = 0U;
      grains_[grainIndex].length = 0U;
    }
    spawn_phase_ = 0.f;
  }

  void freezeLoop()
  {
    updateLoopGeometry();
    frozen_ = true;
    stretch_smooth_ = stretch_target_;
    play_window_ = loop_length_;
    if (captured_samples_ < loop_length_)
      play_window_ = captured_samples_;
    if (play_window_ < kMinLoopSamples)
      play_window_ = kMinLoopSamples;

    const uint32_t newest_index = write_pos_ == 0U ? record_length_ - 1U : write_pos_ - 1U;
    loop_origin_ = wrapU32(static_cast<int32_t>(newest_index) - static_cast<int32_t>(play_window_) + 1,
                           record_length_);

    const uint32_t phase_in_bar = loop_length_ == 0U ? 0U : (captured_samples_ % loop_length_);
    analysis_pos_ = static_cast<float>(phase_in_bar);
    if (analysis_pos_ >= static_cast<float>(play_window_))
      analysis_pos_ = 0.f;

    resetGrains();
    const uint32_t grain_length = grainLengthSamples();
    spawnGrain(0U, analysis_pos_, grain_length);
    const float hop_in = hopInSamples(grain_length);
    spawnGrain(1U, wrapPlayPos(analysis_pos_ + hop_in), grain_length);
    spawn_phase_ = 0.f;

    float seed_left = 0.f;
    float seed_right = 0.f;
    readLoopAtNoGlue(analysis_pos_, seed_left, seed_right);
    loop_lp_left_ = seed_left;
    loop_lp_right_ = seed_right;
    loop_lp2_left_ = seed_left;
    loop_lp2_right_ = seed_right;
  }

  void recordSample(float left, float right)
  {
    if (loop_left_ == nullptr || record_length_ == 0U)
      return;

    loop_left_[write_pos_] = left;
    loop_right_[write_pos_] = right;
    write_pos_ = (write_pos_ + 1U) % record_length_;
    if (captured_samples_ < record_length_)
      ++captured_samples_;
  }

  void advanceWet()
  {
    const float fade_seconds = fadeSeconds();
    float increment = 1.f / (fade_seconds * getSampleRate());
    if (increment > 1.f)
      increment = 1.f;

    if (wet_ < wet_target_)
    {
      wet_ += increment;
      if (wet_ > wet_target_)
        wet_ = wet_target_;
    }
    else if (wet_ > wet_target_)
    {
      wet_ -= increment;
      if (wet_ < wet_target_)
        wet_ = wet_target_;
    }

    if (!pad_held_ && wet_ <= 0.f && frozen_)
    {
      frozen_ = false;
      updateLoopGeometry();
    }
  }

  float fadeSeconds() const
  {
    const float min_beats = 0.125f;
    const float max_beats = 8.f;
    const float beats = min_beats * powf(max_beats / min_beats, time_norm_);
    float seconds = beats * 60.f / bpm_;
    if (seconds < 0.01f)
      seconds = 0.01f;
    return seconds;
  }

  uint32_t grainLengthSamples() const
  {
    uint32_t length = 512U + static_cast<uint32_t>(size_norm_ * 1536.f);
    if (length < 256U)
      length = 256U;
    if (length > play_window_ / 2U && play_window_ > 512U)
      length = play_window_ / 2U;
    return length;
  }

  float hopInSamples(uint32_t grain_length) const
  {
    const float hop_out = static_cast<float>(grain_length) * 0.5f;
    float stretch = stretch_smooth_;
    if (stretch < 0.35f)
      stretch = 0.35f;
    if (stretch > 2.2f)
      stretch = 2.2f;
    return hop_out / stretch;
  }

  float wrapPlayPos(float position) const
  {
    const float window = static_cast<float>(play_window_ == 0U ? 1U : play_window_);
    while (position >= window)
      position -= window;
    while (position < 0.f)
      position += window;
    return position;
  }

  void spawnGrain(uint32_t grain_index, float src_pos, uint32_t length)
  {
    grains_[grain_index].active = true;
    grains_[grain_index].src_pos = wrapPlayPos(src_pos);
    grains_[grain_index].age = 0U;
    grains_[grain_index].length = length;
  }

  float hann(uint32_t age, uint32_t length) const
  {
    if (length <= 1U)
      return 0.f;
    const float phase = static_cast<float>(age) / static_cast<float>(length - 1U);
    return 0.5f * (1.f - cosf(phase * kTwoPi));
  }

  void readLoopAt(float play_pos, float &left, float &right) const
  {
    const float window = static_cast<float>(play_window_);
    float pos = wrapPlayPos(play_pos);
    const int32_t base = static_cast<int32_t>(pos);
    const float frac = pos - static_cast<float>(base);

    float tap_left[4];
    float tap_right[4];
    for (int32_t tapOffset = -1; tapOffset <= 2; ++tapOffset)
    {
      const uint32_t play_index = wrapU32(base + tapOffset, play_window_);
      const uint32_t absolute = wrapU32(static_cast<int32_t>(loop_origin_) + static_cast<int32_t>(play_index),
                                        record_length_);
      tap_left[tapOffset + 1] = loop_left_[absolute];
      tap_right[tapOffset + 1] = loop_right_[absolute];
    }

    left = hermite(tap_left[0], tap_left[1], tap_left[2], tap_left[3], frac);
    right = hermite(tap_right[0], tap_right[1], tap_right[2], tap_right[3], frac);

    const float glue = static_cast<float>(glue_length_);
    if (glue >= 8.f && pos > window - glue)
    {
      const float fade = (pos - (window - glue)) / glue;
      const float start_pos = pos - window + glue;
      float start_left = 0.f;
      float start_right = 0.f;
      readLoopAtNoGlue(start_pos, start_left, start_right);
      const float out_gain = equalPowerOut(fade);
      const float in_gain = equalPowerIn(fade);
      left = left * out_gain + start_left * in_gain;
      right = right * out_gain + start_right * in_gain;
    }
  }

  void readLoopAtNoGlue(float play_pos, float &left, float &right) const
  {
    float pos = wrapPlayPos(play_pos);
    const int32_t base = static_cast<int32_t>(pos);
    const float frac = pos - static_cast<float>(base);

    float tap_left[4];
    float tap_right[4];
    for (int32_t tapOffset = -1; tapOffset <= 2; ++tapOffset)
    {
      const uint32_t play_index = wrapU32(base + tapOffset, play_window_);
      const uint32_t absolute = wrapU32(static_cast<int32_t>(loop_origin_) + static_cast<int32_t>(play_index),
                                        record_length_);
      tap_left[tapOffset + 1] = loop_left_[absolute];
      tap_right[tapOffset + 1] = loop_right_[absolute];
    }

    left = hermite(tap_left[0], tap_left[1], tap_left[2], tap_left[3], frac);
    right = hermite(tap_right[0], tap_right[1], tap_right[2], tap_right[3], frac);
  }

  float amdfAt(float pos_a, float pos_b, uint32_t compare_length) const
  {
    float error = 0.f;
    const uint32_t stride = 4U;
    uint32_t taps = 0U;
    for (uint32_t tapIndex = 0; tapIndex < compare_length; tapIndex += stride)
    {
      float a_left = 0.f;
      float a_right = 0.f;
      float b_left = 0.f;
      float b_right = 0.f;
      readLoopAtNoGlue(pos_a + static_cast<float>(tapIndex), a_left, a_right);
      readLoopAtNoGlue(pos_b + static_cast<float>(tapIndex), b_left, b_right);
      const float a_mono = a_left + a_right;
      const float b_mono = b_left + b_right;
      const float delta = a_mono - b_mono;
      error += delta >= 0.f ? delta : -delta;
      ++taps;
    }
    if (taps == 0U)
      return 0.f;
    return error / static_cast<float>(taps);
  }

  float wsolaOffset(float expected_pos, float reference_pos) const
  {
    const int32_t radius = 48;
    const int32_t stride = 4;
    float best_error = 1.0e9f;
    int32_t best_offset = 0;
    for (int32_t offset = -radius; offset <= radius; offset += stride)
    {
      const float candidate = expected_pos + static_cast<float>(offset);
      const float error = amdfAt(reference_pos, candidate, 64U);
      if (error < best_error)
      {
        best_error = error;
        best_offset = offset;
      }
    }
    return static_cast<float>(best_offset);
  }

  void updateStretch()
  {
    float target = stretch_target_;
    if (type_ == TYPE_BRK)
    {
      if (wet_target_ >= 1.f)
        target = 0.45f + stretch_target_ * wet_;
      else
        target = stretch_target_ * (0.35f + 0.65f * wet_);
    }
    stretch_smooth_ += (target - stretch_smooth_) * 0.0015f;
  }

  void updatePlayWindow()
  {
    play_window_ = loop_length_;
    if (play_window_ > captured_samples_ && captured_samples_ >= kMinLoopSamples)
      play_window_ = captured_samples_;

    if (type_ != TYPE_ROLL)
      return;

    const float roll_amount = wet_target_ >= 1.f ? wet_ : (1.f - wet_);
    uint32_t shifts = static_cast<uint32_t>(roll_amount * 4.f + 0.0001f);
    if (sync_ == SYNC_16)
      shifts += 1U;
    else if (sync_ == SYNC_8)
      shifts += 0U;
    else if (sync_ == SYNC_2)
      shifts = shifts > 0U ? shifts - 1U : 0U;

    uint32_t window = loop_length_ >> shifts;
    const uint32_t min_window = loop_length_ / kStepsPerBar;
    if (window < min_window)
      window = min_window;
    if (window < kMinLoopSamples)
      window = kMinLoopSamples;
    play_window_ = window;
  }

  void renderLoop(float &left, float &right)
  {
    updateStretch();
    updatePlayWindow();

    const bool near_unity = stretch_smooth_ > 0.97f && stretch_smooth_ < 1.03f && type_ != TYPE_BRK;
    if (near_unity)
    {
      readLoopAt(analysis_pos_, left, right);
      analysis_pos_ = wrapPlayPos(analysis_pos_ + 1.f);
      resetGrains();
      return;
    }

    const uint32_t grain_length = grainLengthSamples();
    const float hop_out = static_cast<float>(grain_length) * 0.5f;
    const float hop_in = hopInSamples(grain_length);

    left = 0.f;
    right = 0.f;
    float env_sum = 0.f;
    for (uint32_t grainIndex = 0; grainIndex < kGrainCount; ++grainIndex)
    {
      Grain &grain = grains_[grainIndex];
      if (!grain.active)
        continue;

      const float env = hann(grain.age, grain.length);
      float grain_left = 0.f;
      float grain_right = 0.f;
      readLoopAt(grain.src_pos, grain_left, grain_right);
      left += grain_left * env;
      right += grain_right * env;
      env_sum += env;

      grain.src_pos = wrapPlayPos(grain.src_pos + 1.f);
      ++grain.age;
      if (grain.age >= grain.length)
        grain.active = false;
    }

    spawn_phase_ += 1.f;
    if (spawn_phase_ >= hop_out)
    {
      spawn_phase_ -= hop_out;
      uint32_t free_grain = kGrainCount;
      for (uint32_t grainIndex = 0; grainIndex < kGrainCount; ++grainIndex)
      {
        if (!grains_[grainIndex].active)
        {
          free_grain = grainIndex;
          break;
        }
      }
      if (free_grain < kGrainCount)
      {
        float expected = wrapPlayPos(analysis_pos_ + hop_in);
        float reference = analysis_pos_;
        for (uint32_t grainIndex = 0; grainIndex < kGrainCount; ++grainIndex)
        {
          if (grains_[grainIndex].active)
            reference = grains_[grainIndex].src_pos;
        }
        expected = wrapPlayPos(expected + wsolaOffset(expected, reference));
        spawnGrain(free_grain, expected, grain_length);
        analysis_pos_ = expected;
      }
    }

    if (env_sum > 1.f)
    {
      const float norm = 1.f / env_sum;
      left *= norm;
      right *= norm;
    }
  }

  void applyOnePole(float &state, float input, float coeff) const
  {
    state += coeff * (input - state);
  }

  float filterCoeff(float hz) const
  {
    float coeff = 1.f - expf(-kTwoPi * hz / getSampleRate());
    if (coeff < 0.00005f)
      coeff = 0.00005f;
    if (coeff > 0.95f)
      coeff = 0.95f;
    return coeff;
  }

  void filterPair(float &lp_left, float &lp_right, float &lp2_left, float &lp2_right, float left, float right,
                  float hz, bool highpass, float &out_left, float &out_right)
  {
    const float coeff = filterCoeff(hz);
    applyOnePole(lp_left, left, coeff);
    applyOnePole(lp_right, right, coeff);
    applyOnePole(lp2_left, lp_left, coeff);
    applyOnePole(lp2_right, lp_right, coeff);
    if (highpass)
    {
      out_left = left - lp2_left;
      out_right = right - lp2_right;
    }
    else
    {
      out_left = lp2_left;
      out_right = lp2_right;
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
    const int32_t read_index = static_cast<int32_t>(echo_pos_) - static_cast<int32_t>(delay_int);
    const uint32_t read_a = wrapU32(read_index, kMaxEchoSamples);
    const uint32_t read_b = wrapU32(read_index + 1, kMaxEchoSamples);
    const float delayed_left = echo_left_[read_a] * 0.6f + echo_left_[read_b] * 0.4f;
    const float delayed_right = echo_right_[read_a] * 0.6f + echo_right_[read_b] * 0.4f;

    const float feedback = 0.42f + send_amount * 0.28f;
    echo_left_[echo_pos_] = left * send_amount + delayed_left * feedback;
    echo_right_[echo_pos_] = right * send_amount + delayed_right * feedback;
    echo_pos_ = (echo_pos_ + 1U) % kMaxEchoSamples;

    left += delayed_left * send_amount;
    right += delayed_right * send_amount;
  }

  void applyTransition(float &live_left, float &live_right, float &loop_left, float &loop_right)
  {
    const float amount = wet_;
    const float live_gain = equalPowerOut(amount);
    const float loop_gain = equalPowerIn(amount);
    const float tone = 0.25f + tone_norm_ * 0.75f;
    const float hpf_max = expLerpHz(400.f, 6500.f, tone);
    const float lpf_min = expLerpHz(400.f, 180.f, tone);

    switch (type_)
    {
    case TYPE_VOL:
      live_left *= live_gain;
      live_right *= live_gain;
      loop_left *= loop_gain;
      loop_right *= loop_gain;
      break;

    case TYPE_HPF:
    {
      float live_hpf_left = live_left;
      float live_hpf_right = live_right;
      float loop_hpf_left = loop_left;
      float loop_hpf_right = loop_right;
      filterPair(live_lp_left_, live_lp_right_, live_lp2_left_, live_lp2_right_, live_left, live_right,
                 expLerpHz(22.f, hpf_max, amount), true, live_hpf_left, live_hpf_right);
      filterPair(loop_lp_left_, loop_lp_right_, loop_lp2_left_, loop_lp2_right_, loop_left, loop_right,
                 expLerpHz(hpf_max, 22.f, amount), true, loop_hpf_left, loop_hpf_right);
      const float live_end = amount > 0.8f ? equalPowerOut((amount - 0.8f) * 5.f) : 1.f;
      const float loop_start = amount < 0.2f ? equalPowerIn(amount * 5.f) : 1.f;
      live_left = live_hpf_left * live_end;
      live_right = live_hpf_right * live_end;
      loop_left = loop_hpf_left * loop_start;
      loop_right = loop_hpf_right * loop_start;
      break;
    }

    case TYPE_LPF:
    {
      float live_lpf_left = live_left;
      float live_lpf_right = live_right;
      float loop_lpf_left = loop_left;
      float loop_lpf_right = loop_right;
      filterPair(live_lp_left_, live_lp_right_, live_lp2_left_, live_lp2_right_, live_left, live_right,
                 expLerpHz(12000.f, lpf_min, amount), false, live_lpf_left, live_lpf_right);
      filterPair(loop_lp_left_, loop_lp_right_, loop_lp2_left_, loop_lp2_right_, loop_left, loop_right,
                 expLerpHz(lpf_min, 12000.f, amount), false, loop_lpf_left, loop_lpf_right);
      live_left = live_lpf_left * live_gain;
      live_right = live_lpf_right * live_gain;
      loop_left = loop_lpf_left * loop_gain;
      loop_right = loop_lpf_right * loop_gain;
      break;
    }

    case TYPE_BASS:
    {
      const float split_hz = 180.f;
      float live_low_left = 0.f;
      float live_low_right = 0.f;
      float loop_low_left = 0.f;
      float loop_low_right = 0.f;
      filterPair(bass_lp_live_left_, bass_lp_live_right_, bass_lp2_live_left_, bass_lp2_live_right_, live_left,
                 live_right, split_hz, false, live_low_left, live_low_right);
      filterPair(bass_lp_loop_left_, bass_lp_loop_right_, bass_lp2_loop_left_, bass_lp2_loop_right_, loop_left,
                 loop_right, split_hz, false, loop_low_left, loop_low_right);
      const float live_high_left = live_left - live_low_left;
      const float live_high_right = live_right - live_low_right;
      const float loop_high_left = loop_left - loop_low_left;
      const float loop_high_right = loop_right - loop_low_right;
      const float bass_swap = amount < 0.5f ? 0.f : clamp01((amount - 0.5f) * 8.f);
      live_left = live_high_left * live_gain + live_low_left * (1.f - bass_swap);
      live_right = live_high_right * live_gain + live_low_right * (1.f - bass_swap);
      loop_left = loop_high_left * loop_gain + loop_low_left * bass_swap;
      loop_right = loop_high_right * loop_gain + loop_low_right * bass_swap;
      break;
    }

    case TYPE_ECHO:
    {
      float outgoing_left = live_left;
      float outgoing_right = live_right;
      applyEcho(outgoing_left, outgoing_right, amount);
      live_left = outgoing_left * live_gain;
      live_right = outgoing_right * live_gain;
      loop_left *= loop_gain;
      loop_right *= loop_gain;
      break;
    }

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
  float stretch_target_ = 1.f;
  float stretch_smooth_ = 1.f;
  float mix_ = 1.f;
  float glue_norm_ = 0.39f;
  float size_norm_ = 0.5f;
  float tone_norm_ = 0.68f;
  float bpm_ = 120.f;
  float wet_ = 0.f;
  float wet_target_ = 0.f;
  float analysis_pos_ = 0.f;
  float spawn_phase_ = 0.f;
  float live_lp_left_ = 0.f;
  float live_lp_right_ = 0.f;
  float live_lp2_left_ = 0.f;
  float live_lp2_right_ = 0.f;
  float loop_lp_left_ = 0.f;
  float loop_lp_right_ = 0.f;
  float loop_lp2_left_ = 0.f;
  float loop_lp2_right_ = 0.f;
  float bass_lp_live_left_ = 0.f;
  float bass_lp_live_right_ = 0.f;
  float bass_lp2_live_left_ = 0.f;
  float bass_lp2_live_right_ = 0.f;
  float bass_lp_loop_left_ = 0.f;
  float bass_lp_loop_right_ = 0.f;
  float bass_lp2_loop_left_ = 0.f;
  float bass_lp2_loop_right_ = 0.f;
  uint32_t loop_length_ = 96000U;
  uint32_t glue_length_ = 2048U;
  uint32_t record_length_ = 98048U;
  uint32_t play_window_ = 96000U;
  uint32_t loop_origin_ = 0U;
  uint32_t write_pos_ = 0U;
  uint32_t echo_pos_ = 0U;
  uint32_t captured_samples_ = 0U;
  uint8_t type_ = TYPE_HPF;
  uint8_t sync_ = SYNC_4;
  bool frozen_ = false;
  bool pad_held_ = false;
  Grain grains_[kGrainCount];
};
