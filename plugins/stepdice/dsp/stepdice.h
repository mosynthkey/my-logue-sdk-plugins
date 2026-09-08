#pragma once

/*
 * File: stepdice.h
 *
 * Tempo-synced step FX sequencer. A bar is divided into 16/8/4/2/1 steps
 * and each step plays a different treatment of AUDIO IN. Pattern assignment
 * is a seeded permutation (Y / touch re-roll), not an LFO.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class StepDice : public Processor
{
public:
  static constexpr uint32_t kMaxBufSamples = 288000U;
  static constexpr uint32_t kMaxSteps = 16U;
  static constexpr uint32_t kXfadeSamples = 64U;
  static constexpr uint32_t kMinSliceSamples = 64U;
  static constexpr float kMinBpm = 40.f;
  static constexpr float kMaxBpm = 300.f;

  uint32_t getBufferSize() const override final { return kMaxBufSamples * 2U; }

  enum
  {
    AMT = 0U,
    DICE,
    MIX,
    STEPS,
    BANK,
    DENS,
    HOLD,
    NUM_PARAMS
  };

  enum
  {
    BANK_ALL = 0,
    BANK_FLT,
    BANK_GLCH,
    BANK_TONE,
    NUM_BANKS
  };

  enum
  {
    HOLD_RUN = 0,
    HOLD_PAD
  };

  enum
  {
    FX_DRY = 0,
    FX_GATE,
    FX_HPF,
    FX_LPF,
    FX_CRUSH,
    FX_RING,
    FX_PANL,
    FX_PANR,
    FX_DIST,
    FX_STUT,
    FX_REV,
    FX_ECHO,
    NUM_FX
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case AMT:
      amt_norm_ = param_10bit_to_f32(value);
      break;
    case DICE:
    {
      const float next = param_10bit_to_f32(value);
      if (si_fabsf(next - dice_norm_) > 0.001f)
        pattern_dirty_ = true;
      dice_norm_ = next;
      break;
    }
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case STEPS:
    {
      const uint8_t next = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, 4.f));
      if (next != steps_sel_)
        pattern_dirty_ = true;
      steps_sel_ = next;
      break;
    }
    case BANK:
    {
      const uint8_t next = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, 3.f));
      if (next != bank_)
        pattern_dirty_ = true;
      bank_ = next;
      break;
    }
    case DENS:
    {
      const float next = param_10bit_to_f32(value);
      if (si_fabsf(next - dens_norm_) > 0.001f)
        pattern_dirty_ = true;
      dens_norm_ = next;
      break;
    }
    case HOLD:
      hold_ = (value != 0) ? HOLD_PAD : HOLD_RUN;
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    static const char *step_names[5] = {"16", "8", "4", "2", "1"};
    static const char *bank_names[NUM_BANKS] = {"ALL", "FLT", "GLCH", "TONE"};
    static const char *hold_names[2] = {"RUN", "HOLD"};

    if (index == STEPS && value >= 0 && value <= 4)
      return step_names[value];
    if (index == BANK && value >= 0 && value < NUM_BANKS)
      return bank_names[value];
    if (index == HOLD && value >= 0 && value <= 1)
      return hold_names[value];
    return nullptr;
  }

  void init(float *allocated_buffer) override final
  {
    buf_left_ = allocated_buffer;
    buf_right_ = allocated_buffer + kMaxBufSamples;
    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;

    amt_norm_ = 0.7f;
    dice_norm_ = 0.2f;
    mix_ = 1.f;
    dens_norm_ = 0.85f;
    steps_sel_ = 0;
    bank_ = BANK_ALL;
    hold_ = HOLD_RUN;
    bpm_ = 120.f;
    throw_ = 1U;
    pattern_dirty_ = true;
    rebuildPattern();
    reset();
  }

  void teardown() override final
  {
    buf_left_ = nullptr;
    buf_right_ = nullptr;
  }

  void reset() override final
  {
    write_pos_ = 0U;
    captured_ = 0U;
    clock_acc_ = 0.f;
    samples_into_step_ = 0.f;
    step_index_ = 0U;
    xfade_ = 1.f;
    pad_held_ = false;
    engaged_ = true;
    ring_phase_ = 0.f;
    crush_hold_left_ = 0.f;
    crush_hold_right_ = 0.f;
    crush_count_ = 0U;
    stut_origin_ = 0U;
    stut_length_ = kMinSliceSamples;
    stut_phase_ = 0U;
    rev_origin_ = 0U;
    rev_length_ = kMinSliceSamples;
    rev_play_ = 0U;
    hp_left_.z = 0.f;
    hp_right_.z = 0.f;
    lp_left_.z = 0.f;
    lp_right_.z = 0.f;
    prev_wet_left_ = 0.f;
    prev_wet_right_ = 0.f;
    if (buf_left_ != nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < kMaxBufSamples; ++sampleIndex)
      {
        buf_left_[sampleIndex] = 0.f;
        buf_right_[sampleIndex] = 0.f;
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
    if (phase == k_unit_touch_phase_began && !pad_held_)
    {
      if (hold_ == HOLD_RUN)
      {
        ++throw_;
        pattern_dirty_ = true;
      }
    }
    pad_held_ = down;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    if (pattern_dirty_)
      rebuildPattern();

    const uint32_t steps = stepsPerBar();
    if (step_index_ >= steps)
      step_index_ = 0U;
    const float step_samples = barSamples() / static_cast<float>(steps);
    const bool run = (hold_ == HOLD_RUN) || pad_held_;
    const float xfade_inc = 1.f / static_cast<float>(kXfadeSamples);
    const float sr = getSampleRate();
    const uint32_t echo_samples = fx::samplesPerBeat(bpm_, sr) / 2U;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      if (buf_left_ != nullptr)
      {
        buf_left_[write_pos_] = live_left;
        buf_right_[write_pos_] = live_right;
        write_pos_ = (write_pos_ + 1U) % kMaxBufSamples;
        if (captured_ < kMaxBufSamples)
          ++captured_;
      }

      clock_acc_ += 1.f;
      samples_into_step_ += 1.f;
      if (clock_acc_ >= step_samples)
      {
        clock_acc_ -= step_samples;
        samples_into_step_ = 0.f;
        step_index_ = (step_index_ + 1U) % steps;
        enterStep(step_index_, step_samples);
        xfade_ = 0.f;
      }

      if (xfade_ < 1.f)
      {
        xfade_ += xfade_inc;
        if (xfade_ > 1.f)
          xfade_ = 1.f;
      }

      const uint8_t fx_id = pattern_fx_[step_index_];
      const float step_amt = fx::clip01(amt_norm_ * pattern_amt_[step_index_]);
      const float step_mod = pattern_mod_[step_index_];
      const float step_phase = (step_samples > 1.f) ? (samples_into_step_ / step_samples) : 0.f;

      float wet_left = live_left;
      float wet_right = live_right;
      renderFx(fx_id, step_amt, step_mod, step_phase, echo_samples, live_left, live_right, wet_left,
               wet_right);

      wet_left = fx::mix(prev_wet_left_, wet_left, xfade_);
      wet_right = fx::mix(prev_wet_right_, wet_right, xfade_);
      prev_wet_left_ = wet_left;
      prev_wet_right_ = wet_right;

      engaged_ += ((run ? 1.f : 0.f) - engaged_) * 0.04f;
      const float amount = mix_ * fx::clip01(engaged_);
      out[0] = fx::mix(live_left, wet_left, amount);
      out[1] = fx::mix(live_right, wet_right, amount);

      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

  uint32_t currentStep() const { return step_index_; }
  uint32_t stepsPerBar() const { return 16U >> steps_sel_; }
  uint8_t stepFx(uint32_t step_index) const { return pattern_fx_[step_index % kMaxSteps]; }
  uint32_t throwCount() const { return throw_; }

private:
  static uint32_t hashU32(uint32_t value)
  {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
  }

  static float hash01(uint32_t &state)
  {
    state = hashU32(state + 0x9e3779b9U);
    return static_cast<float>(state >> 8) * (1.f / 16777216.f);
  }

  float barSamples() const
  {
    return static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()) * 4U);
  }

  void rebuildPattern()
  {
    static const uint8_t kAll[] = {FX_GATE, FX_HPF, FX_LPF, FX_CRUSH, FX_RING, FX_PANL,
                                   FX_PANR, FX_DIST, FX_STUT, FX_REV, FX_ECHO};
    static const uint8_t kFlt[] = {FX_GATE, FX_HPF, FX_LPF, FX_PANL, FX_PANR};
    static const uint8_t kGlch[] = {FX_GATE, FX_CRUSH, FX_STUT, FX_REV, FX_ECHO};
    static const uint8_t kTone[] = {FX_HPF, FX_LPF, FX_RING, FX_DIST};

    const uint8_t *palette = kAll;
    uint32_t palette_size = sizeof(kAll);
    if (bank_ == BANK_FLT)
    {
      palette = kFlt;
      palette_size = sizeof(kFlt);
    }
    else if (bank_ == BANK_GLCH)
    {
      palette = kGlch;
      palette_size = sizeof(kGlch);
    }
    else if (bank_ == BANK_TONE)
    {
      palette = kTone;
      palette_size = sizeof(kTone);
    }

    uint8_t deck[kMaxSteps];
    for (uint32_t slotIndex = 0; slotIndex < kMaxSteps; ++slotIndex)
      deck[slotIndex] = palette[slotIndex % palette_size];

    uint32_t rng = hashU32(1U + throw_ + static_cast<uint32_t>(dice_norm_ * 1023.f) * 2654435761U +
                           static_cast<uint32_t>(bank_) * 97U);
    for (uint32_t shuffleIndex = kMaxSteps - 1U; shuffleIndex > 0U; --shuffleIndex)
    {
      const uint32_t swapIndex = static_cast<uint32_t>(hash01(rng) * static_cast<float>(shuffleIndex + 1U));
      const uint8_t tmp = deck[shuffleIndex];
      deck[shuffleIndex] = deck[swapIndex % kMaxSteps];
      deck[swapIndex % kMaxSteps] = tmp;
    }

    for (uint32_t stepIndex = 0; stepIndex < kMaxSteps; ++stepIndex)
    {
      uint8_t fx_id = deck[stepIndex];
      if (stepIndex > 0U && fx_id == pattern_fx_[stepIndex - 1U] && palette_size > 1U)
        fx_id = deck[(stepIndex + 3U) % kMaxSteps];
      const bool dry = hash01(rng) > dens_norm_;
      pattern_fx_[stepIndex] = dry ? static_cast<uint8_t>(FX_DRY) : fx_id;
      pattern_amt_[stepIndex] = 0.4f + hash01(rng) * 0.6f;
      pattern_mod_[stepIndex] = hash01(rng);
    }

    pattern_dirty_ = false;
    enterStep(step_index_, barSamples() / static_cast<float>(stepsPerBar()));
  }

  void enterStep(uint32_t step_index, float step_samples)
  {
    const uint8_t fx_id = pattern_fx_[step_index % kMaxSteps];
    const uint32_t available = captured_ < kMaxBufSamples ? captured_ : kMaxBufSamples;
    uint32_t slice = static_cast<uint32_t>(step_samples);
    if (slice < kMinSliceSamples)
      slice = kMinSliceSamples;
    if (slice > available && available > kMinSliceSamples)
      slice = available;

    if (fx_id == FX_STUT)
    {
      uint32_t stut = slice >> 2;
      if (stut < kMinSliceSamples)
        stut = kMinSliceSamples;
      stut_length_ = stut;
      stut_origin_ = write_pos_;
      stut_phase_ = 0U;
    }
    else if (fx_id == FX_REV)
    {
      rev_length_ = slice;
      rev_origin_ = write_pos_;
      rev_play_ = 0U;
    }
    crush_count_ = 0U;
  }

  void readBuffer(uint32_t index, float &left, float &right) const
  {
    const uint32_t wrapped = index % kMaxBufSamples;
    if (buf_left_ == nullptr)
    {
      left = 0.f;
      right = 0.f;
      return;
    }
    left = buf_left_[wrapped];
    right = buf_right_[wrapped];
  }

  void renderFx(uint8_t fx_id, float amt, float mod, float step_phase, uint32_t echo_samples,
                float live_left, float live_right, float &out_left, float &out_right)
  {
    const float hp_hz = 180.f + amt * (600.f + mod * 3600.f);
    const float lp_hz = 420.f + (1.f - amt) * (1800.f + mod * 5200.f);
    const float hp_c = fx::onePoleCoeff(hp_hz, getSampleRate());
    const float lp_c = fx::onePoleCoeff(lp_hz, getSampleRate());
    const float hp_left = hp_left_.processHp(live_left, hp_c);
    const float hp_right = hp_right_.processHp(live_right, hp_c);
    const float lp_left = lp_left_.processLp(live_left, lp_c);
    const float lp_right = lp_right_.processLp(live_right, lp_c);

    out_left = live_left;
    out_right = live_right;

    switch (fx_id)
    {
    case FX_GATE:
    {
      const float duty = fx::clip(1.f - amt * 0.92f, 0.04f, 1.f);
      const float gate = (step_phase < duty) ? 1.f : (1.f - amt);
      out_left = live_left * gate;
      out_right = live_right * gate;
      break;
    }
    case FX_HPF:
      out_left = fx::mix(live_left, hp_left, 0.55f + amt * 0.45f);
      out_right = fx::mix(live_right, hp_right, 0.55f + amt * 0.45f);
      break;
    case FX_LPF:
      out_left = fx::mix(live_left, lp_left, 0.55f + amt * 0.45f);
      out_right = fx::mix(live_right, lp_right, 0.55f + amt * 0.45f);
      break;
    case FX_CRUSH:
    {
      const uint32_t hold = 1U + static_cast<uint32_t>(amt * (8.f + mod * 28.f));
      if (crush_count_ == 0U)
      {
        crush_hold_left_ = live_left;
        crush_hold_right_ = live_right;
        crush_count_ = hold;
      }
      else
      {
        --crush_count_;
      }
      const float levels = 2.f + (1.f - amt) * 22.f;
      out_left = static_cast<float>(static_cast<int32_t>(crush_hold_left_ * levels)) / levels;
      out_right = static_cast<float>(static_cast<int32_t>(crush_hold_right_ * levels)) / levels;
      break;
    }
    case FX_RING:
    {
      ring_phase_ += (70.f + mod * 740.f) / getSampleRate();
      if (ring_phase_ >= 1.f)
        ring_phase_ -= 1.f;
      const float carrier = fastersinfullf(ring_phase_ * 6.283185307179586f);
      const float depth = 0.35f + amt * 0.65f;
      out_left = live_left * fx::mix(1.f, carrier, depth);
      out_right = live_right * fx::mix(1.f, carrier, depth);
      break;
    }
    case FX_PANL:
      out_left = live_left * (1.f + amt * 0.25f);
      out_right = live_right * (1.f - amt);
      break;
    case FX_PANR:
      out_left = live_left * (1.f - amt);
      out_right = live_right * (1.f + amt * 0.25f);
      break;
    case FX_DIST:
    {
      const float drive = 1.f + amt * (6.f + mod * 6.f);
      out_left = fastertanhf(live_left * drive);
      out_right = fastertanhf(live_right * drive);
      break;
    }
    case FX_STUT:
    {
      if (captured_ < stut_length_)
        break;
      const uint32_t read = (stut_origin_ + kMaxBufSamples - stut_length_ + stut_phase_) % kMaxBufSamples;
      readBuffer(read, out_left, out_right);
      stut_phase_ = (stut_phase_ + 1U) % stut_length_;
      break;
    }
    case FX_REV:
    {
      if (captured_ < kMinSliceSamples)
        break;
      ++rev_play_;
      if (rev_play_ > rev_length_)
        rev_play_ = rev_length_;
      const uint32_t read = (rev_origin_ + kMaxBufSamples - rev_play_) % kMaxBufSamples;
      readBuffer(read, out_left, out_right);
      break;
    }
    case FX_ECHO:
    {
      uint32_t delay = echo_samples;
      if (mod > 0.5f)
        delay = echo_samples / 2U;
      if (delay < 64U)
        delay = 64U;
      if (delay >= kMaxBufSamples)
        delay = kMaxBufSamples - 1U;
      float delayed_left = 0.f;
      float delayed_right = 0.f;
      readBuffer(write_pos_ + kMaxBufSamples - delay, delayed_left, delayed_right);
      out_left = live_left + delayed_left * (0.35f + amt * 0.7f);
      out_right = live_right + delayed_right * (0.35f + amt * 0.7f);
      break;
    }
    default:
      break;
    }
  }

  float *buf_left_ = nullptr;
  float *buf_right_ = nullptr;
  uint32_t write_pos_ = 0U;
  uint32_t captured_ = 0U;
  uint32_t step_index_ = 0U;
  uint32_t throw_ = 1U;
  uint32_t crush_count_ = 0U;
  uint32_t stut_origin_ = 0U;
  uint32_t stut_length_ = kMinSliceSamples;
  uint32_t stut_phase_ = 0U;
  uint32_t rev_origin_ = 0U;
  uint32_t rev_length_ = kMinSliceSamples;
  uint32_t rev_play_ = 0U;
  float clock_acc_ = 0.f;
  float samples_into_step_ = 0.f;
  float bpm_ = 120.f;
  float amt_norm_ = 0.7f;
  float dice_norm_ = 0.2f;
  float mix_ = 1.f;
  float dens_norm_ = 0.85f;
  float xfade_ = 1.f;
  float engaged_ = 1.f;
  float ring_phase_ = 0.f;
  float crush_hold_left_ = 0.f;
  float crush_hold_right_ = 0.f;
  float prev_wet_left_ = 0.f;
  float prev_wet_right_ = 0.f;
  fx::OnePole hp_left_;
  fx::OnePole hp_right_;
  fx::OnePole lp_left_;
  fx::OnePole lp_right_;
  uint8_t pattern_fx_[kMaxSteps] = {};
  float pattern_amt_[kMaxSteps] = {};
  float pattern_mod_[kMaxSteps] = {};
  uint8_t steps_sel_ = 0;
  uint8_t bank_ = BANK_ALL;
  uint8_t hold_ = HOLD_RUN;
  bool pad_held_ = false;
  bool pattern_dirty_ = true;
};
