#pragma once

/*
 * File: grainpad.h
 *
 * NTS-3 kaoss pad granular synth wrapper for GrainPad.
 */

#include "grainpad_engine.h"
#include "processor.h"
#include "runtime.h"
#include <math.h>
#include <stdint.h>

class GrainPad : public Processor
{
public:
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
    pad_active_ = false;
    pad_releasing_ = false;
  }

  void reset() override final
  {
    engine_.reset();
    pad_active_ = false;
    pad_releasing_ = false;
  }

  void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    (void)id;

    const float scan_norm = static_cast<float>(x) * (1.f / 1023.f);
    const float pitch_ratio = pitchFromPadY(y);

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      pad_active_ = false;
      pad_releasing_ = true;
      engine_.setTouchTargets(scan_norm, pitch_ratio, false, true);
      return;
    }

    if (phase != k_unit_touch_phase_began && phase != k_unit_touch_phase_moved &&
        phase != k_unit_touch_phase_stationary)
      return;

    pad_active_ = true;
    pad_releasing_ = false;
    engine_.setParameter(GrainPadEngine::SCAN, static_cast<int32_t>(x));
    engine_.setParameter(GrainPadEngine::PITCH, static_cast<int32_t>(y));
    engine_.setTouchTargets(scan_norm, pitch_ratio, true, false);
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    const float wet_gain = engine_.outputLevel();
    const float wet_amt = stereo_mix_ ? engine_.mix() : 1.f;
    const float dry_amt = stereo_mix_ ? (1.f - engine_.mix()) : 0.f;

    for (uint32_t blockStart = 0; blockStart < frames;)
    {
      const uint32_t blockSize = (frames - blockStart) > 64U ? 64U : (frames - blockStart);
      engine_.renderBlock(mono_scratch_, blockSize);

      for (uint32_t sampleIndex = 0; sampleIndex < blockSize; ++sampleIndex)
      {
        const float wet = mono_scratch_[sampleIndex] * wet_gain;

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

      blockStart += blockSize;
    }
  }

private:
  static float pitchFromPadY(uint32_t y)
  {
    const float norm = static_cast<float>(y) * (1.f / 1023.f);
    const float octaves = (norm - 0.5f) * 4.f;
    return powf(2.f, octaves);
  }

  bool stereo_mix_ = false;
  bool pad_active_ = false;
  bool pad_releasing_ = false;
  float mono_scratch_[64];
  GrainPadEngine engine_;
};
