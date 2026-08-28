#pragma once

/*
 * File: airfm.h
 *
 * Two-operator phase-modulation FM inspired by Alesis airSynth Program 3 "FM".
 * NTS-3 genericfx oscillator: pad XY controls carrier/modulator frequency, touch gates output.
 *
 */

#include "processor.h"
#include "unit_genericfx.h"
#include <math.h>
#include <stdint.h>

class AirFM : public Processor
{
public:
  static constexpr float kTwoPi = 6.283185307179586f;
  static constexpr float kTouchNorm = 1.f / 1023.f;
  static constexpr float kF1BaseHz = 40.f;
  static constexpr float kF1Ratio = 200.f;
  static constexpr float kF2MinHz = 55.f;
  static constexpr float kHpfHz = 18.f;
  static constexpr float kLpfHz = 7500.f;
  static constexpr float kFadeTimeSec = 0.01f;
  static constexpr float kFeedback = 0.f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    INDEX = 0U,
    YMAX,
    DRIVE,
    LEVEL,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case INDEX:
      index_rad_ = value * 0.01f;
      break;
    case YMAX:
      ymax_hz_ = static_cast<float>(value);
      updateFrequencies();
      break;
    case DRIVE:
      drive_ = value * 0.01f;
      break;
    case LEVEL:
      level_ = value * 0.01f;
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    (void)index;
    (void)value;
    return nullptr;
  }

  void init(float *) override final
  {
    index_rad_ = 4.f;
    ymax_hz_ = 550.f;
    drive_ = 1.08f;
    level_ = 0.28f;
    pad_x_ = 0.5f;
    pad_y_ = 0.5f;
    amp_env_ = 0.f;
    amp_env_step_ = 0.f;
    clearSignalState();
    updateFilterCoeffs();
    updateFrequencies();
  }

  void reset() override final
  {
    clearSignalState();
    amp_env_ = 0.f;
    amp_env_step_ = 0.f;
  }

  void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    (void)id;

    if (phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
        phase == k_unit_touch_phase_stationary)
    {
      pad_x_ = static_cast<float>(x) * kTouchNorm;
      pad_y_ = static_cast<float>(y) * kTouchNorm;
      updateFrequencies();
      amp_env_ = 1.f;
      amp_env_step_ = 0.f;
      return;
    }

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      startReleaseFade();
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    (void)in;

    const float sample_rate = getSampleRate();
    const float carrier_inc_scale = kTwoPi / sample_rate;
    const float mod_inc_scale = carrier_inc_scale;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      const float mod = sinf(ph_m_ + kFeedback * mod_prev_);
      mod_prev_ = mod;

      const float carrier = sinf(ph_c_ + index_rad_ * mod);
      const float shaped = tanhf(drive_ * carrier);
      const float amp = level_ * amp_env_;
      const float voice = amp * shaped;

      const float blocked = voice - dc_prev_in_ + hpf_a_ * dc_prev_out_;
      dc_prev_in_ = voice;
      dc_prev_out_ = blocked;

      lpf_state_ += lpf_a_ * (blocked - lpf_state_);

      advanceAmpEnvelope();

      ph_c_ += carrier_inc_scale * f1_hz_;
      ph_m_ += mod_inc_scale * f2_hz_;
      wrapPhase(ph_c_);
      wrapPhase(ph_m_);

      const float output_sample = lpf_state_;
      out[0] = output_sample;
      out[1] = output_sample;
      out += 2;
    }
  }

private:
  static void wrapPhase(float &phase)
  {
    if (phase >= kTwoPi)
      phase -= kTwoPi;
    else if (phase < 0.f)
      phase += kTwoPi;
  }

  void clearSignalState()
  {
    ph_c_ = 0.f;
    ph_m_ = 0.f;
    mod_prev_ = 0.f;
    dc_prev_in_ = 0.f;
    dc_prev_out_ = 0.f;
    lpf_state_ = 0.f;
  }

  void updateFilterCoeffs()
  {
    const float sample_rate = getSampleRate();
    hpf_a_ = expf(-kTwoPi * kHpfHz / sample_rate);
    lpf_a_ = 1.f - expf(-kTwoPi * kLpfHz / sample_rate);
  }

  void updateFrequencies()
  {
    f1_hz_ = kF1BaseHz * powf(kF1Ratio, pad_x_);
    f2_hz_ = kF2MinHz * powf(ymax_hz_ / kF2MinHz, pad_y_);
  }

  void startReleaseFade()
  {
    const float fade_samples = kFadeTimeSec * getSampleRate();
    if (fade_samples <= 0.f)
    {
      amp_env_ = 0.f;
      amp_env_step_ = 0.f;
      return;
    }
    amp_env_step_ = -amp_env_ / fade_samples;
    if (amp_env_step_ >= 0.f)
      amp_env_step_ = -1.f / fade_samples;
  }

  void advanceAmpEnvelope()
  {
    if (amp_env_step_ == 0.f)
      return;

    amp_env_ += amp_env_step_;
    if (amp_env_step_ < 0.f)
    {
      if (amp_env_ <= 0.f)
      {
        amp_env_ = 0.f;
        amp_env_step_ = 0.f;
      }
    }
    else if (amp_env_ >= 1.f)
    {
      amp_env_ = 1.f;
      amp_env_step_ = 0.f;
    }
  }

  float ph_c_ = 0.f;
  float ph_m_ = 0.f;
  float mod_prev_ = 0.f;
  float dc_prev_in_ = 0.f;
  float dc_prev_out_ = 0.f;
  float lpf_state_ = 0.f;
  float hpf_a_ = 0.f;
  float lpf_a_ = 0.f;

  float f1_hz_ = 440.f;
  float f2_hz_ = 193.f;
  float pad_x_ = 0.5f;
  float pad_y_ = 0.5f;

  float index_rad_ = 4.f;
  float ymax_hz_ = 550.f;
  float drive_ = 1.08f;
  float level_ = 0.28f;

  float amp_env_ = 0.f;
  float amp_env_step_ = 0.f;
};
