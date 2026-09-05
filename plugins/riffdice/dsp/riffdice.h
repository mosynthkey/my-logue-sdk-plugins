#pragma once

/*
 * File: riffdice.h
 *
 * Conditional 16-step riff generator. Y scales 1:2 and percent trigs.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class RiffDice : public Processor
{
public:
  static constexpr uint32_t kMaxSteps = 16U;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    ROOT = 0U,
    PROB,
    MIX,
    SCALE,
    LEN,
    SLIDE,
    OCT,
    NUM_PARAMS
  };

  enum
  {
    SCALE_MIN = 0,
    SCALE_MAJ,
    SCALE_DOR,
    SCALE_PENT
  };

  enum
  {
    COND_ALWAYS = 0,
    COND_HALF,
    COND_PCT
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case ROOT:
      root_note_ = static_cast<int8_t>(fx::clip(static_cast<float>(value), 24.f, 48.f));
      break;
    case PROB:
      prob_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case SCALE:
      scale_sel_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, 3.f));
      break;
    case LEN:
      length_16_ = value != 0;
      break;
    case SLIDE:
      slide_norm_ = param_10bit_to_f32(value);
      break;
    case OCT:
      octave_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, 2.f));
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index == SCALE)
    {
      static const char *kNames[] = {"MIN", "MAJ", "DOR", "PENT"};
      if (value < 0)
        value = 0;
      if (value > 3)
        value = 3;
      return kNames[value];
    }
    if (index == LEN)
      return (value != 0) ? "16" : "8";
    if (index == OCT)
    {
      if (value <= 0)
        return "-1";
      if (value == 1)
        return "0";
      return "+1";
    }
    if (index != ROOT)
      return nullptr;
    static char label[8];
    static const char *kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int32_t note = value;
    if (note < 0)
      note = 0;
    if (note > 127)
      note = 127;
    char *out = label;
    const char *name = kNames[note % 12];
    while (*name)
      *out++ = *name++;
    *out++ = static_cast<char>('0' + (note / 12) - 1);
    *out = '\0';
    return label;
  }

  void init(float *) override final
  {
    bpm_ = 120.f;
    running_ = false;
    clock_acc_ = 0.f;
    step_index_ = 0U;
    cycle_ = 0U;
    rng_ = 31U;
    env_ = 0.f;
    cutoff_z_ = 0.f;
    phase_ = 0.f;
    pitch_ = 36.f;
    pitch_target_ = 36.f;
    buildPattern();
  }

  void reset() override final
  {
    running_ = false;
    env_ = 0.f;
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 40.f && tempo < 300.f)
      bpm_ = tempo;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      running_ = false;
      return;
    }
    if (phase == k_unit_touch_phase_began && !running_)
    {
      running_ = true;
      step_index_ = 0U;
      clock_acc_ = 0.f;
      cycle_ = 0U;
      buildPattern();
      tryTrigger(0U);
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    (void)raw;
    const uint32_t length = length_16_ ? 16U : 8U;
    const float step_samples = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate())) * 0.25f;
    const float slide_coeff = 0.0008f + (1.f - slide_norm_) * 0.02f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      if (running_)
      {
        clock_acc_ += 1.f;
        if (clock_acc_ >= step_samples)
        {
          clock_acc_ -= step_samples;
          step_index_ = (step_index_ + 1U) % length;
          if (step_index_ == 0U)
            ++cycle_;
          tryTrigger(step_index_);
        }
      }

      pitch_ += (pitch_target_ - pitch_) * slide_coeff;
      const float inc = fx::noteToInc(pitch_, getSampleRate());
      phase_ = fx::wrap01(phase_ + inc);
      float saw = fx::blepSaw(phase_, inc);
      cutoff_z_ += 0.08f * (saw - cutoff_z_);
      saw = saw - cutoff_z_ * 0.35f;
      env_ *= 0.9994f;
      const float wet = fx::softclip(saw * env_ * 0.9f);
      out[0] = fx::mix(in[0], wet, mix_);
      out[1] = fx::mix(in[1], wet, mix_);
      in += 2;
      out += 2;
    }
  }

private:
  struct Step
  {
    int8_t degree;
    uint8_t cond;
    bool slide;
  };

  void buildPattern()
  {
    static const int8_t kDefaultDeg[kMaxSteps] = {0, 0, 3, 0, 7, 3, 0, 10, 0, 3, 7, 0, 12, 7, 3, 0};
    static const uint8_t kDefaultCond[kMaxSteps] = {
        COND_ALWAYS, COND_PCT, COND_HALF, COND_ALWAYS, COND_HALF, COND_PCT, COND_ALWAYS, COND_PCT,
        COND_ALWAYS, COND_HALF, COND_PCT, COND_ALWAYS, COND_PCT, COND_HALF, COND_PCT, COND_ALWAYS};
    for (uint32_t stepIndex = 0; stepIndex < kMaxSteps; ++stepIndex)
    {
      phrase_[stepIndex].degree = kDefaultDeg[stepIndex];
      phrase_[stepIndex].cond = kDefaultCond[stepIndex];
      phrase_[stepIndex].slide = (stepIndex % 5U) == 4U;
    }
  }

  int8_t scaleInterval(int8_t degree) const
  {
    static const int8_t kMin[] = {0, 2, 3, 5, 7, 8, 10};
    static const int8_t kMaj[] = {0, 2, 4, 5, 7, 9, 11};
    static const int8_t kDor[] = {0, 2, 3, 5, 7, 9, 10};
    static const int8_t kPent[] = {0, 3, 5, 7, 10, 12, 15};
    const int8_t *scale = kMin;
    if (scale_sel_ == SCALE_MAJ)
      scale = kMaj;
    else if (scale_sel_ == SCALE_DOR)
      scale = kDor;
    else if (scale_sel_ == SCALE_PENT)
      scale = kPent;
    int32_t octave = 0;
    int32_t idx = degree;
    while (idx < 0)
    {
      idx += 7;
      --octave;
    }
    octave += idx / 7;
    idx %= 7;
    return static_cast<int8_t>(scale[idx] + octave * 12);
  }

  bool condFires(uint8_t cond)
  {
    if (cond == COND_ALWAYS)
      return true;
    if (cond == COND_HALF)
      return ((cycle_ & 1U) == 0U) || (fx::randomFloat(rng_) < prob_norm_);
    return fx::randomFloat(rng_) < (0.08f + prob_norm_ * 0.7f);
  }

  void tryTrigger(uint32_t step)
  {
    const Step &s = phrase_[step];
    if (!condFires(s.cond))
      return;
    const float note = static_cast<float>(root_note_ + (static_cast<int>(octave_) - 1) * 12 + scaleInterval(s.degree));
    pitch_target_ = note;
    if (!s.slide)
      pitch_ = note;
    env_ = 1.f;
  }

  Step phrase_[kMaxSteps];
  float clock_acc_ = 0.f;
  float bpm_ = 120.f;
  float prob_norm_ = 0.5f;
  float slide_norm_ = 0.3f;
  float mix_ = 1.f;
  float env_ = 0.f;
  float cutoff_z_ = 0.f;
  float phase_ = 0.f;
  float pitch_ = 36.f;
  float pitch_target_ = 36.f;
  uint32_t step_index_ = 0U;
  uint32_t cycle_ = 0U;
  uint32_t rng_ = 31U;
  int8_t root_note_ = 36;
  uint8_t scale_sel_ = SCALE_MIN;
  uint8_t octave_ = 1;
  bool length_16_ = true;
  bool running_ = false;
};
