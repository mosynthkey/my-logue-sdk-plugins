#pragma once

/*
 * File: osc.h
 *
 * Pulse-width oscillator with a sine wavefolder for NTS-1 mkII.
 *
 */

#include "processor.h"
#include "unit_osc.h"

class Osc : public Processor
{
public:
  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    WIDTH = 0U,
    FOLD,
    MODE,
    NUM_PARAMS
  };

  enum
  {
    MODE_PULSE = 0,
    MODE_FOLD,
    MODE_MIX,
    NUM_MODES
  };

  struct Params
  {
    float width;
    float fold;
    uint32_t mode;

    void reset()
    {
      width = 0.5f;
      fold = 0.f;
      mode = MODE_PULSE;
    }

    Params() { reset(); }
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case WIDTH:
      params_.width = param_10bit_to_f32(value);
      break;
    case FOLD:
      params_.fold = param_10bit_to_f32(value);
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
        "PULSE",
        "FOLD",
        "MIX",
    };

    if (index == MODE && value >= MODE_PULSE && value < NUM_MODES)
      return mode_strings[value];

    return nullptr;
  }

  void init(float *) override final
  {
    params_.reset();
    phasor_ = 0.f;
    w0_ = 440.f / getSampleRate();
    lfo_ = 0.f;
  }

  void setPitch(float w0)
  {
    w0_ = w0;
  }

  void setShapeLfo(float lfo)
  {
    lfo_ = lfo;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    (void)in;
    const Params p = params_;
    const float pulseWidth = 0.03f + clip01f(p.width + lfo_ * 0.15f) * 0.94f;
    const float foldAmount = clip01f(p.fold);
    const float increment = w0_;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex, ++out)
    {
      phasor_ += increment;
      if (phasor_ >= 1.f)
        phasor_ -= 1.f;

      float pulse = phasor_ < pulseWidth ? 1.f : -1.f;
      pulse += polyblep(phasor_, increment);
      float wrapPhase = phasor_ - pulseWidth;
      if (wrapPhase < 0.f)
        wrapPhase += 1.f;
      pulse -= polyblep(wrapPhase, increment);

      const float folded = osc_sinf(pulse * (0.25f + foldAmount * 1.75f));

      float sample = pulse;
      if (p.mode == MODE_FOLD)
        sample = folded;
      else if (p.mode == MODE_MIX)
        sample = pulse + (folded - pulse) * foldAmount;

      out[0] = sample;
    }
  }

private:
  static float polyblep(float phase, float increment)
  {
    if (increment <= 0.f)
      return 0.f;

    if (phase < increment)
    {
      const float t = phase / increment;
      return t + t - t * t - 1.f;
    }

    if (phase > 1.f - increment)
    {
      const float t = (phase - 1.f) / increment;
      return t * t + t + t + 1.f;
    }

    return 0.f;
  }

  Params params_;
  float w0_;
  float lfo_;
  float phasor_;
};
