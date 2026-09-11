#pragma once

/*
 * File: kopunch.h
 *
 * KO II Punch-In FX inspired XY pad for NTS-3. Continuously captures AUDIO IN
 * (prefer get_raw_input). Pad up bypasses. Hold engages one of twelve modes:
 * pitch random, slice swap, granulizer, beat repeat, tape stop, filter LFO,
 * LPF, HPF, send/delay throw, tremolo, octave down, decimator.
 * X = mode, Y = pressure, Depth = mix.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class KoPunch : public Processor
{
public:
  static constexpr uint32_t kMaxBufSamples = 192000U;
  static constexpr uint32_t kMaxDelaySamples = 48000U;
  static constexpr uint32_t kMinSliceSamples = 64U;
  static constexpr uint32_t kMinCaptureSamples = 1024U;
  static constexpr uint32_t kXfadeSamples = 96U;
  static constexpr uint32_t kGrainCount = 3U;
  static constexpr float kMinBpm = 40.f;
  static constexpr float kMaxBpm = 300.f;
  static constexpr float kMinCapturePeak = 0.003f;
  static constexpr float kWetFadeSamples = 256.f;

  uint32_t getBufferSize() const override final
  {
    return kMaxBufSamples * 2U + kMaxDelaySamples * 2U;
  }

  enum
  {
    MODE = 0U,
    PRESS,
    MIX,
    COLOR,
    HOLD,
    NUM_PARAMS
  };

  enum
  {
    MODE_PRND = 0,
    MODE_SWAP,
    MODE_GRAN,
    MODE_RPT,
    MODE_TAPE,
    MODE_FLFO,
    MODE_LPF,
    MODE_HPF,
    MODE_SEND,
    MODE_TREM,
    MODE_OCTD,
    MODE_DEC,
    NUM_MODES
  };

  enum
  {
    HOLD_GATE = 0,
    HOLD_LATCH,
    NUM_HOLDS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case MODE:
    {
      int32_t mode = value;
      if (mode < 0)
        mode = 0;
      if (mode >= NUM_MODES)
        mode = NUM_MODES - 1;
      if (mode_ != static_cast<uint8_t>(mode))
      {
        mode_ = static_cast<uint8_t>(mode);
        if (active_)
          engageCurrentMode();
      }
      break;
    }
    case PRESS:
      press_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case COLOR:
      color_norm_ = param_10bit_to_f32(value);
      break;
    case HOLD:
    {
      int32_t hold = value;
      if (hold < 0)
        hold = 0;
      if (hold >= NUM_HOLDS)
        hold = NUM_HOLDS - 1;
      hold_ = static_cast<uint8_t>(hold);
      if (hold_ == HOLD_GATE && !pad_held_ && active_)
        requestRelease();
      break;
    }
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    static const char *mode_names[NUM_MODES] = {
        "PRND", "SWAP", "GRAN", "RPT", "TAPE", "FLFO",
        "LPF", "HPF", "SEND", "TREM", "OCTD", "DEC"};
    static const char *hold_names[NUM_HOLDS] = {"GATE", "LATCH"};

    if (index == MODE && value >= 0 && value < NUM_MODES)
      return mode_names[value];
    if (index == HOLD && value >= 0 && value < NUM_HOLDS)
      return hold_names[value];
    return nullptr;
  }

  void init(float *allocated_buffer) override final
  {
    buf_left_ = allocated_buffer;
    buf_right_ = allocated_buffer + kMaxBufSamples;
    delay_left_ = allocated_buffer + kMaxBufSamples * 2U;
    delay_right_ = allocated_buffer + kMaxBufSamples * 2U + kMaxDelaySamples;

    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;

    press_norm_ = 0.6f;
    mix_ = 1.f;
    color_norm_ = 0.4f;
    mode_ = MODE_RPT;
    hold_ = HOLD_GATE;
    bpm_ = 120.f;
    updateLoopGeometry();
    reset();
  }

  void teardown() override final
  {
    buf_left_ = nullptr;
    buf_right_ = nullptr;
    delay_left_ = nullptr;
    delay_right_ = nullptr;
  }

  void reset() override final
  {
    write_pos_ = 0U;
    delay_pos_ = 0U;
    captured_samples_ = 0U;
    arm_samples_ = 0U;
    captured_peak_ = 0.f;
    pad_held_ = false;
    active_ = false;
    arming_ = false;
    wet_ = 0.f;
    wet_target_ = 0.f;
    play_pos_ = 0.f;
    play_rate_ = 1.f;
    tape_progress_ = 0.f;
    lfo_phase_ = 0.f;
    trem_phase_ = 0.f;
    pitch_hold_ = 1.f;
    pitch_timer_ = 0.f;
    crush_hold_left_ = 0.f;
    crush_hold_right_ = 0.f;
    crush_counter_ = 0.f;
    grain_spawn_ = 0.f;
    stretch_read_ = 0.f;
    swap_origin_ = 0U;
    frozen_origin_ = 0U;
    frozen_length_ = 0U;
    slice_length_ = kMinSliceSamples;
    rng_state_ = 0xC0FFEE42U;
    delay_fb_left_.z = 0.f;
    delay_fb_right_.z = 0.f;
    filt_left_.z = 0.f;
    filt_right_.z = 0.f;
    resetSvf();

    for (uint32_t grainIndex = 0; grainIndex < kGrainCount; ++grainIndex)
    {
      grains_[grainIndex].age = 1.f;
      grains_[grainIndex].pos = 0.f;
      grains_[grainIndex].length = 1.f;
      grains_[grainIndex].rate = 1.f;
    }
  }

  void setTempo(float tempo) override final
  {
    if (tempo >= kMinBpm && tempo <= kMaxBpm)
    {
      bpm_ = tempo;
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
      if (hold_ == HOLD_GATE)
        requestRelease();
      return;
    }

    if (phase != k_unit_touch_phase_began && phase != k_unit_touch_phase_moved &&
        phase != k_unit_touch_phase_stationary)
      return;

    const bool new_touch = !pad_held_;
    pad_held_ = true;
    if (phase == k_unit_touch_phase_began || new_touch)
      engageCurrentMode();
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out,
               uint32_t frames)
  {
    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      const bool freeze_playback = needsFrozenAudio() && !arming_ && (active_ || wet_ > 0.f);
      if (!freeze_playback)
        recordSample(live_left, live_right);

      if (arming_)
      {
        if (arm_samples_ < 0xFFFFFFF0U)
          ++arm_samples_;
        if (arm_samples_ >= neededCaptureSamples() && captured_peak_ >= kMinCapturePeak)
        {
          arming_ = false;
          freezeWindow(neededCaptureSamples());
          startVoice();
          wet_target_ = 1.f;
        }
      }

      advanceWet();

      if (wet_ <= 0.f)
      {
        out[0] = live_left;
        out[1] = live_right;
        in += 2;
        if (raw != nullptr)
          raw += 2;
        out += 2;
        continue;
      }

      float fx_left = live_left;
      float fx_right = live_right;
      renderMode(live_left, live_right, fx_left, fx_right);

      const float wet_gain = wet_ * mix_;
      out[0] = fx::mix(live_left, fx_left, wet_gain);
      out[1] = fx::mix(live_right, fx_right, wet_gain);

      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

  bool isActive() const { return active_; }
  bool isPadHeld() const { return pad_held_; }
  uint8_t currentMode() const { return mode_; }

private:
  struct Grain
  {
    float pos;
    float age;
    float length;
    float rate;
  };

  struct SvfState
  {
    float ic1 = 0.f;
    float ic2 = 0.f;
  };

  bool needsFrozenAudio() const
  {
    return mode_ == MODE_PRND || mode_ == MODE_SWAP || mode_ == MODE_GRAN || mode_ == MODE_RPT ||
           mode_ == MODE_TAPE || mode_ == MODE_OCTD;
  }

  uint32_t neededCaptureSamples() const
  {
    if (mode_ == MODE_TAPE)
    {
      // Capture a short frozen window; spindown length is separate (press_).
      uint32_t samples = static_cast<uint32_t>(0.5f * 60.f / bpm_ * getSampleRate());
      if (samples < kMinCaptureSamples)
        samples = kMinCaptureSamples;
      if (samples > record_length_)
        samples = record_length_;
      return samples;
    }
    if (mode_ == MODE_GRAN || mode_ == MODE_SWAP || mode_ == MODE_PRND || mode_ == MODE_OCTD)
    {
      uint32_t samples = loop_length_;
      if (samples < kMinCaptureSamples)
        samples = kMinCaptureSamples;
      return samples;
    }
    uint32_t samples = sliceFromPressure();
    if (samples < kMinCaptureSamples)
      samples = kMinCaptureSamples;
    if (samples > record_length_)
      samples = record_length_;
    return samples;
  }

  uint32_t sliceFromPressure() const
  {
    static const float kBeats[6] = {2.f, 1.f, 0.5f, 0.25f, 0.125f, 0.0625f};
    const float select = press_norm_ * 5.0001f;
    uint32_t step = static_cast<uint32_t>(select);
    if (step > 5U)
      step = 5U;
    float samples = kBeats[step] * 60.f / bpm_ * getSampleRate();
    if (samples < static_cast<float>(kMinSliceSamples))
      samples = static_cast<float>(kMinSliceSamples);
    if (samples > static_cast<float>(record_length_))
      samples = static_cast<float>(record_length_);
    return static_cast<uint32_t>(samples + 0.5f);
  }

  void requestRelease()
  {
    wet_target_ = 0.f;
    arming_ = false;
  }

  void engageCurrentMode()
  {
    active_ = true;
    arming_ = false;
    rng_state_ ^= write_pos_ + 0x9E3779B9U;
    resetSvf();
    delay_fb_left_.z = 0.f;
    delay_fb_right_.z = 0.f;
    filt_left_.z = 0.f;
    filt_right_.z = 0.f;

    if (!needsFrozenAudio())
    {
      startVoice();
      wet_target_ = 1.f;
      return;
    }

    const uint32_t needed = neededCaptureSamples();
    if (captured_samples_ >= needed && captured_peak_ >= kMinCapturePeak)
    {
      freezeWindow(needed);
      startVoice();
      wet_target_ = 1.f;
      return;
    }

    arming_ = true;
    arm_samples_ = 0U;
    captured_peak_ = 0.f;
    wet_target_ = 0.f;
  }

  void startVoice()
  {
    play_pos_ = 0.f;
    play_rate_ = 1.f;
    tape_progress_ = 0.f;
    lfo_phase_ = 0.f;
    trem_phase_ = 0.f;
    pitch_hold_ = 1.f;
    pitch_timer_ = 0.f;
    crush_counter_ = 0.f;
    grain_spawn_ = 0.f;
    stretch_read_ = 0.f;
    swap_origin_ = 0U;
    slice_length_ = sliceFromPressure();
    if (slice_length_ > frozen_length_ && frozen_length_ >= kMinSliceSamples)
      slice_length_ = frozen_length_;

    for (uint32_t grainIndex = 0; grainIndex < kGrainCount; ++grainIndex)
    {
      grains_[grainIndex].age = 1.f;
      grains_[grainIndex].pos = 0.f;
      grains_[grainIndex].length = 1.f;
      grains_[grainIndex].rate = 1.f;
    }

    if (mode_ == MODE_SWAP)
      pickSwapSlice();
    if (mode_ == MODE_GRAN)
    {
      spawnGrain(0U);
      spawnGrain(1U);
      spawnGrain(2U);
    }
    if (mode_ == MODE_PRND)
      reseedPitch();
    if (mode_ == MODE_OCTD)
      play_rate_ = 0.5f;
  }

  void freezeWindow(uint32_t length)
  {
    if (length > captured_samples_)
      length = captured_samples_;
    if (length < kMinSliceSamples)
      length = kMinSliceSamples;
    if (length > record_length_)
      length = record_length_;

    frozen_length_ = length;
    const uint32_t newest_index = write_pos_ == 0U ? record_length_ - 1U : write_pos_ - 1U;
    int32_t origin = static_cast<int32_t>(newest_index) - static_cast<int32_t>(length) + 1;
    if (origin < 0)
      origin += static_cast<int32_t>(record_length_);
    frozen_origin_ = static_cast<uint32_t>(origin);
  }

  void updateLoopGeometry()
  {
    const float seconds_per_bar = 240.f / bpm_;
    uint32_t samples = static_cast<uint32_t>(seconds_per_bar * getSampleRate() + 0.5f);
    if (samples < kMinCaptureSamples)
      samples = kMinCaptureSamples;
    if (samples > kMaxBufSamples)
      samples = kMaxBufSamples;
    loop_length_ = samples;
    record_length_ = samples;
  }

  void recordSample(float left, float right)
  {
    if (buf_left_ == nullptr || record_length_ == 0U)
      return;
    buf_left_[write_pos_] = left;
    buf_right_[write_pos_] = right;
    ++write_pos_;
    if (write_pos_ >= record_length_)
      write_pos_ = 0U;
    if (captured_samples_ < record_length_)
      ++captured_samples_;
    const float peak = si_fabsf(left) > si_fabsf(right) ? si_fabsf(left) : si_fabsf(right);
    if (peak > captured_peak_)
      captured_peak_ = peak;
  }

  void advanceWet()
  {
    const float increment = 1.f / kWetFadeSamples;
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
    if (wet_ <= 0.f && wet_target_ <= 0.f && !arming_)
      active_ = false;
  }

  float nextRandom()
  {
    return fx::randomFloat(rng_state_);
  }

  static uint32_t wrapIndex(uint32_t index, uint32_t length)
  {
    if (length == 0U)
      return 0U;
    while (index >= length)
      index -= length;
    return index;
  }

  void readFrozen(float pos, uint32_t window, float &left, float &right) const
  {
    if (buf_left_ == nullptr || window == 0U)
    {
      left = 0.f;
      right = 0.f;
      return;
    }

    float wrapped = pos;
    const float window_f = static_cast<float>(window);
    while (wrapped >= window_f)
      wrapped -= window_f;
    while (wrapped < 0.f)
      wrapped += window_f;

    const uint32_t index_a = static_cast<uint32_t>(wrapped);
    const float frac = wrapped - static_cast<float>(index_a);
    const uint32_t index_b = wrapIndex(index_a + 1U, window);
    const uint32_t abs_a = wrapIndex(frozen_origin_ + index_a, record_length_);
    const uint32_t abs_b = wrapIndex(frozen_origin_ + index_b, record_length_);
    left = buf_left_[abs_a] + (buf_left_[abs_b] - buf_left_[abs_a]) * frac;
    right = buf_right_[abs_a] + (buf_right_[abs_b] - buf_right_[abs_a]) * frac;
  }

  void readFrozenWithXfade(float pos, uint32_t window, float &left, float &right) const
  {
    readFrozen(pos, window, left, right);
    if (window <= kXfadeSamples + 8U)
      return;
    const float xfade = static_cast<float>(kXfadeSamples);
    const float window_f = static_cast<float>(window);
    if (pos + xfade >= window_f)
    {
      const float fade = (pos + xfade - window_f) / xfade;
      float start_left = 0.f;
      float start_right = 0.f;
      readFrozen(pos + xfade - window_f, window, start_left, start_right);
      left = left * (1.f - fade) + start_left * fade;
      right = right * (1.f - fade) + start_right * fade;
    }
  }

  void pickSwapSlice()
  {
    uint32_t window = sliceFromPressure();
    if (window > frozen_length_)
      window = frozen_length_ < kMinSliceSamples ? kMinSliceSamples : frozen_length_;
    slice_length_ = window;
    const uint32_t range = frozen_length_ > window ? frozen_length_ - window : 0U;
    swap_origin_ = range == 0U ? 0U : static_cast<uint32_t>(nextRandom() * static_cast<float>(range));
    play_pos_ = 0.f;
  }

  void reseedPitch()
  {
    // Pressure maps how far pitch falls (1 → ~0.35).
    const float depth = 0.15f + press_norm_ * 0.7f;
    pitch_hold_ = 1.f - nextRandom() * depth;
    if (pitch_hold_ < 0.25f)
      pitch_hold_ = 0.25f;
    pitch_timer_ = getSampleRate() * (0.04f + nextRandom() * 0.18f);
  }

  void spawnGrain(uint32_t grainIndex)
  {
    const float min_grain = getSampleRate() * (0.004f + press_norm_ * 0.004f);
    const float max_grain = getSampleRate() * (0.02f + (1.f - press_norm_) * 0.10f);
    const float grain_samples = min_grain + (max_grain - min_grain) * nextRandom();
    grains_[grainIndex].length = grain_samples < 8.f ? 8.f : grain_samples;
    grains_[grainIndex].age = 0.f;
    grains_[grainIndex].rate = 0.85f + nextRandom() * 0.3f;
    float pos = stretch_read_ + (nextRandom() * 2.f - 1.f) * color_norm_ * grain_samples;
    if (pos < 0.f)
      pos = 0.f;
    if (frozen_length_ > 1U && pos > static_cast<float>(frozen_length_ - 1U))
      pos = static_cast<float>(frozen_length_ - 1U);
    grains_[grainIndex].pos = pos;
  }

  void resetSvf()
  {
    svf_left_.ic1 = 0.f;
    svf_left_.ic2 = 0.f;
    svf_right_.ic1 = 0.f;
    svf_right_.ic2 = 0.f;
  }

  void processSvf(float input, SvfState &state, float cutoff_hz, float resonance, float &lp,
                  float &hp)
  {
    const float g =
        fastertanfullf(3.14159265f * fx::clip(cutoff_hz, 40.f, 16000.f) / getSampleRate());
    const float k = 2.f - 1.7f * fx::clip01(resonance);
    const float a1 = 1.f / (1.f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;
    const float v3 = input - state.ic2;
    const float v1 = a1 * state.ic1 + a2 * v3;
    const float v2 = state.ic2 + a2 * state.ic1 + a3 * v3;
    state.ic1 = 2.f * v1 - state.ic1;
    state.ic2 = 2.f * v2 - state.ic2;
    lp = v2;
    hp = input - k * v1 - v2;
  }

  void applyCrush(float &left, float &right, float amount)
  {
    if (amount <= 0.001f)
      return;
    const float period = 1.f + amount * amount * 120.f;
    crush_counter_ -= 1.f;
    if (crush_counter_ <= 0.f)
    {
      crush_counter_ = period;
      const float bits = 12.f - amount * 10.f;
      float scale = fasterpowf(2.f, bits - 1.f);
      if (scale < 1.f)
        scale = 1.f;
      crush_hold_left_ = static_cast<float>(static_cast<int32_t>(left * scale)) / scale;
      crush_hold_right_ = static_cast<float>(static_cast<int32_t>(right * scale)) / scale;
    }
    left = crush_hold_left_;
    right = crush_hold_right_;
  }

  void renderMode(float live_left, float live_right, float &left, float &right)
  {
    switch (mode_)
    {
    case MODE_PRND:
      renderPitchRandom(left, right);
      break;
    case MODE_SWAP:
      renderSwap(left, right);
      break;
    case MODE_GRAN:
      renderGran(left, right);
      break;
    case MODE_RPT:
      renderRepeat(left, right);
      break;
    case MODE_TAPE:
      renderTape(left, right);
      break;
    case MODE_FLFO:
      renderFilterLfo(live_left, live_right, left, right);
      break;
    case MODE_LPF:
      renderFilter(live_left, live_right, left, right, true);
      break;
    case MODE_HPF:
      renderFilter(live_left, live_right, left, right, false);
      break;
    case MODE_SEND:
      renderSend(live_left, live_right, left, right);
      break;
    case MODE_TREM:
      renderTremolo(live_left, live_right, left, right);
      break;
    case MODE_OCTD:
      renderOctaveDown(left, right);
      break;
    case MODE_DEC:
      renderDecimator(live_left, live_right, left, right);
      break;
    default:
      left = live_left;
      right = live_right;
      break;
    }
  }

  void renderPitchRandom(float &left, float &right)
  {
    const uint32_t window = frozen_length_ < kMinSliceSamples ? kMinSliceSamples : frozen_length_;
    pitch_timer_ -= 1.f;
    if (pitch_timer_ <= 0.f)
      reseedPitch();
    readFrozen(play_pos_, window, left, right);
    play_pos_ += pitch_hold_;
    if (play_pos_ >= static_cast<float>(window))
      play_pos_ -= static_cast<float>(window);
  }

  void renderSwap(float &left, float &right)
  {
    const uint32_t window = slice_length_ < kMinSliceSamples ? kMinSliceSamples : slice_length_;
    float pos = play_pos_ + static_cast<float>(swap_origin_);
    readFrozenWithXfade(pos, frozen_length_ < window ? window : frozen_length_, left, right);
    play_pos_ += 1.f;
    if (play_pos_ >= static_cast<float>(window))
      pickSwapSlice();
  }

  void renderGran(float &left, float &right)
  {
    const float density = 0.35f + color_norm_ * 0.65f;
    const float hop = grains_[0].length > 8.f ? grains_[0].length * (0.35f + (1.f - density) * 0.5f)
                                              : getSampleRate() * 0.02f;
    grain_spawn_ += 1.f;
    if (grain_spawn_ >= hop)
    {
      grain_spawn_ -= hop;
      uint32_t oldest = 0U;
      float oldest_age = grains_[0].age;
      for (uint32_t grainIndex = 1; grainIndex < kGrainCount; ++grainIndex)
      {
        if (grains_[grainIndex].age > oldest_age)
        {
          oldest_age = grains_[grainIndex].age;
          oldest = grainIndex;
        }
      }
      spawnGrain(oldest);
    }

    left = 0.f;
    right = 0.f;
    const uint32_t window = frozen_length_ < kMinSliceSamples ? kMinSliceSamples : frozen_length_;
    for (uint32_t grainIndex = 0; grainIndex < kGrainCount; ++grainIndex)
    {
      Grain &grain = grains_[grainIndex];
      if (grain.age >= 1.f)
        continue;
      float g_left = 0.f;
      float g_right = 0.f;
      readFrozen(grain.pos, window, g_left, g_right);
      const float env = grain.age < 0.5f ? grain.age * 2.f : (1.f - grain.age) * 2.f;
      left += g_left * env;
      right += g_right * env;
      grain.pos += grain.rate;
      grain.age += 1.f / grain.length;
    }
    stretch_read_ += 0.35f + (1.f - press_norm_) * 0.9f;
    if (window > 1U && stretch_read_ >= static_cast<float>(window))
      stretch_read_ -= static_cast<float>(window);
  }

  void renderRepeat(float &left, float &right)
  {
    slice_length_ = sliceFromPressure();
    if (slice_length_ > frozen_length_ && frozen_length_ >= kMinSliceSamples)
      slice_length_ = frozen_length_;
    const uint32_t window = slice_length_ < kMinSliceSamples ? kMinSliceSamples : slice_length_;
    readFrozenWithXfade(play_pos_, window, left, right);
    play_pos_ += 1.f;
    if (play_pos_ >= static_cast<float>(window))
      play_pos_ -= static_cast<float>(window);
  }

  void renderTape(float &left, float &right)
  {
    const uint32_t window = frozen_length_ < kMinSliceSamples ? kMinSliceSamples : frozen_length_;
    const float stop_beats = 0.25f + press_norm_ * 3.75f;
    const float stop_samples = stop_beats * 60.f / bpm_ * getSampleRate();
    tape_progress_ += 1.f / (stop_samples < 64.f ? 64.f : stop_samples);
    if (tape_progress_ > 1.f)
      tape_progress_ = 1.f;
    const float remain = 1.f - tape_progress_;
    play_rate_ = remain * remain;
    if (play_rate_ < 0.02f)
    {
      left = 0.f;
      right = 0.f;
      return;
    }
    readFrozen(play_pos_, window, left, right);
    play_pos_ += play_rate_;
    if (play_pos_ >= static_cast<float>(window))
    {
      left = 0.f;
      right = 0.f;
    }
  }

  void renderFilterLfo(float live_left, float live_right, float &left, float &right)
  {
    const float speed_hz = 0.2f + press_norm_ * press_norm_ * 12.f;
    lfo_phase_ += speed_hz / getSampleRate();
    if (lfo_phase_ >= 1.f)
      lfo_phase_ -= 1.f;
    const float lfo = 0.5f + 0.5f * fastersinfullf(lfo_phase_ * 6.2831853f);
    const float cutoff = 120.f + lfo * (12000.f + color_norm_ * 4000.f);
    const float resonance = 0.35f + color_norm_ * 0.55f;
    float lp = 0.f;
    float hp = 0.f;
    processSvf(live_left, svf_left_, cutoff, resonance, lp, hp);
    left = lp;
    processSvf(live_right, svf_right_, cutoff, resonance, lp, hp);
    right = lp;
  }

  void renderFilter(float live_left, float live_right, float &left, float &right, bool lowpass)
  {
    float cutoff;
    if (lowpass)
      // Harder pressure closes the filter (more muffling).
      cutoff = 120.f + (1.f - press_norm_) * (1.f - press_norm_) * 14000.f;
    else
      // Harder pressure opens the HPF (thinner), but keep musical headroom.
      cutoff = 80.f + press_norm_ * press_norm_ * 6000.f;
    const float resonance = 0.4f + color_norm_ * 0.55f;
    float lp = 0.f;
    float hp = 0.f;
    processSvf(live_left, svf_left_, cutoff, resonance, lp, hp);
    left = lowpass ? lp : hp;
    processSvf(live_right, svf_right_, cutoff, resonance, lp, hp);
    right = lowpass ? lp : hp;
  }

  void renderSend(float live_left, float live_right, float &left, float &right)
  {
    if (delay_left_ == nullptr)
    {
      left = live_left;
      right = live_right;
      return;
    }

    const float delay_beats = 0.375f; // dotted 8th
    uint32_t delay_samples =
        static_cast<uint32_t>(delay_beats * 60.f / bpm_ * getSampleRate() + 0.5f);
    if (delay_samples < 64U)
      delay_samples = 64U;
    if (delay_samples >= kMaxDelaySamples)
      delay_samples = kMaxDelaySamples - 1U;

    const uint32_t read_pos =
        (delay_pos_ + kMaxDelaySamples - delay_samples) % kMaxDelaySamples;
    float delayed_left = delay_left_[read_pos];
    float delayed_right = delay_right_[read_pos];

    const float tone_coeff = fx::onePoleCoeff(800.f + color_norm_ * 6000.f, getSampleRate());
    delayed_left = delay_fb_left_.processLp(delayed_left, tone_coeff);
    delayed_right = delay_fb_right_.processLp(delayed_right, tone_coeff);

    const float depth = press_norm_;
    const float feedback = 0.15f + color_norm_ * 0.65f;
    delay_left_[delay_pos_] = live_left + delayed_left * feedback;
    delay_right_[delay_pos_] = live_right + delayed_right * feedback;
    ++delay_pos_;
    if (delay_pos_ >= kMaxDelaySamples)
      delay_pos_ = 0U;

    left = live_left + delayed_left * depth * 1.4f;
    right = live_right + delayed_right * depth * 1.4f;
  }

  void renderTremolo(float live_left, float live_right, float &left, float &right)
  {
    const float speed_hz = 1.f + press_norm_ * press_norm_ * 18.f;
    trem_phase_ += speed_hz / getSampleRate();
    if (trem_phase_ >= 1.f)
      trem_phase_ -= 1.f;
    const float depth = 0.35f + color_norm_ * 0.65f;
    const float mod = 1.f - depth * (0.5f + 0.5f * fastersinfullf(trem_phase_ * 6.2831853f));
    left = live_left * mod;
    right = live_right * mod;
  }

  void renderOctaveDown(float &left, float &right)
  {
    const uint32_t window = frozen_length_ < kMinSliceSamples ? kMinSliceSamples : frozen_length_;
    const float target_rate = 0.5f + (1.f - press_norm_) * 0.5f;
    play_rate_ += 0.002f * (target_rate - play_rate_);
    readFrozenWithXfade(play_pos_, window, left, right);
    play_pos_ += play_rate_;
    if (play_pos_ >= static_cast<float>(window))
      play_pos_ -= static_cast<float>(window);
  }

  void renderDecimator(float live_left, float live_right, float &left, float &right)
  {
    left = live_left;
    right = live_right;
    applyCrush(left, right, press_norm_);
    const float tone = fx::onePoleCoeff(300.f + (1.f - color_norm_) * 8000.f, getSampleRate());
    left = filt_left_.processLp(left, tone);
    right = filt_right_.processLp(right, tone);
  }

  float *buf_left_ = nullptr;
  float *buf_right_ = nullptr;
  float *delay_left_ = nullptr;
  float *delay_right_ = nullptr;

  float press_norm_ = 0.6f;
  float mix_ = 1.f;
  float color_norm_ = 0.4f;
  float bpm_ = 120.f;
  float wet_ = 0.f;
  float wet_target_ = 0.f;
  float play_pos_ = 0.f;
  float play_rate_ = 1.f;
  float tape_progress_ = 0.f;
  float lfo_phase_ = 0.f;
  float trem_phase_ = 0.f;
  float pitch_hold_ = 1.f;
  float pitch_timer_ = 0.f;
  float crush_hold_left_ = 0.f;
  float crush_hold_right_ = 0.f;
  float crush_counter_ = 0.f;
  float grain_spawn_ = 0.f;
  float stretch_read_ = 0.f;
  float captured_peak_ = 0.f;

  uint32_t write_pos_ = 0U;
  uint32_t delay_pos_ = 0U;
  uint32_t captured_samples_ = 0U;
  uint32_t arm_samples_ = 0U;
  uint32_t record_length_ = 0U;
  uint32_t loop_length_ = 0U;
  uint32_t frozen_origin_ = 0U;
  uint32_t frozen_length_ = 0U;
  uint32_t slice_length_ = 0U;
  uint32_t swap_origin_ = 0U;
  uint32_t rng_state_ = 0U;

  uint8_t mode_ = MODE_RPT;
  uint8_t hold_ = HOLD_GATE;
  bool pad_held_ = false;
  bool active_ = false;
  bool arming_ = false;

  Grain grains_[kGrainCount];
  SvfState svf_left_;
  SvfState svf_right_;
  fx::OnePole delay_fb_left_;
  fx::OnePole delay_fb_right_;
  fx::OnePole filt_left_;
  fx::OnePole filt_right_;
};
