#pragma once

/*
 * File: airhorn.h
 *
 * NTS-1 mkII / NTS-3 Processor wrapper for the AirHorn engine.
 */

#include "airhorn_engine.h"
#include "processor.h"
#include <stdint.h>

class AirHorn : public Processor
{
public:
  static constexpr float kPlaybackRate = AirHornEngine::kPlaybackRate;

  uint32_t getBufferSize() const override final { return 0; }

  void setStereoMix(bool enabled) { stereo_mix_ = enabled; }

  void setParameter(uint8_t index, int32_t value) override final
  {
    engine_.setParameter(index, value);
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    return engine_.getParameterStrValue(index, value);
  }

  void init(float *) override final
  {
    engine_.init();
  }

  void reset() override final { engine_.reset(); }

  void setPitch(float w0)
  {
    (void)w0;
  }

  void noteOn(uint8_t note, uint8_t velo) override final
  {
    engine_.startVoice(velo, note);
  }

  void noteOff(uint8_t note) override final
  {
    engine_.releaseNote(note);
  }

  void allNoteOff() override final
  {
    engine_.releaseAll();
  }

  void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    (void)id;
    (void)x;
    (void)y;

    if (phase == 0U)
      engine_.startVoice(127, 0);
    else if (phase == 2U || phase == 4U)
      engine_.releaseAll();
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    const float wet_gain = engine_.outputLevel();
    const float wet_amt = stereo_mix_ ? engine_.mix() : 1.f;
    const float dry_amt = stereo_mix_ ? (1.f - engine_.mix()) : 0.f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      const float wet = engine_.renderMono() * wet_gain;

      if (stereo_mix_)
      {
        out[0] = in[0] * dry_amt + wet * wet_amt;
        out[1] = in[1] * dry_amt + wet * wet_amt;
        in += 2;
        out += 2;
      }
      else
      {
        out[0] = wet;
        ++out;
      }
    }
  }

  float outputLevel() const { return engine_.outputLevel(); }

private:
  bool stereo_mix_ = false;
  AirHornEngine engine_;
};
