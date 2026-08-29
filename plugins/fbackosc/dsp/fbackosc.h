#pragma once

/*
 * File: fbackosc.h
 *
 * NTS-1 mkII Processor wrapper for the Feedback oscillator engine.
 *
 */

#include "fbackosc_engine.h"
#include "processor.h"
#include "macros.h"
#include <stdint.h>

class FBackOsc : public Processor
{
public:
  enum
  {
    HARMONICS = 0U,
    FEEDBACK,
    NUM_PARAMS
  };

  uint32_t getBufferSize() const override final { return 0; }

  void setParameter(uint8_t index, int32_t value) override final
  {
    FBackOscEngine::Params params = engine_.getParams();

    switch (index)
    {
    case HARMONICS:
      params.harmonics = param_10bit_to_f32(value);
      break;
    case FEEDBACK:
      params.feedback = param_10bit_to_f32(value);
      break;
    default:
      return;
    }

    engine_.setParams(params);
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    (void)index;
    (void)value;
    return nullptr;
  }

  void init(float *) override final
  {
    FBackOscEngine::Params params;
    params.harmonics = 0.5f;
    params.feedback = 0.45f;
    engine_.setParams(params);
    engine_.reset();
    engine_.randomizePhase();
    base_note_ = 60.f;
    base_w0_ = 261.625565f * (1.f / getSampleRate());
    engine_.setPitch(base_w0_, base_note_);
  }

  void reset() override final
  {
    engine_.reset();
    engine_.randomizePhase();
  }

  void setPitch(float w0)
  {
    base_w0_ = w0;
    engine_.setPitch(base_w0_, base_note_);
  }

  void setNote(float note)
  {
    base_note_ = note;
    engine_.setPitch(base_w0_, base_note_);
  }

  void noteOn(uint8_t note, uint8_t velo) override final
  {
    (void)velo;
    base_note_ = static_cast<float>(note);
    engine_.setPitch(base_w0_, base_note_);
    engine_.reset();
    engine_.randomizePhase();
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    (void)in;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
      out[sampleIndex] = engine_.render();
  }

private:
  FBackOscEngine engine_;
  float base_w0_ = 0.f;
  float base_note_ = 60.f;
};
