#pragma once

/*
 * File: speccloud.h
 *
 * Logarithmic spectral-band clouds. FFT magnitudes are grouped into log
 * bands whose gains are randomly re-rolled, then overlap-added back.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "spectralfft.h"
#include <stdint.h>

class SpecCloud : public Processor
{
public:
  static constexpr uint32_t kFftSize = 512U;
  static constexpr uint32_t kHopSize = 128U;
  static constexpr uint32_t kNumBins = kFftSize / 2U + 1U;
  static constexpr uint32_t kMaxBands = 32U;
  static constexpr float kOlaGain = 0.42f;
  static constexpr float kMagFloor = 1e-10f;

  uint32_t getBufferSize() const override final
  {
    return kFftSize * 4U + kNumBins * 2U + kMaxBands * 2U;
  }

  enum
  {
    BANDS = 0U,
    FLUX,
    MIX,
    SMOOTH,
    SEED,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case BANDS:
      bands_norm_ = param_10bit_to_f32(value);
      break;
    case FLUX:
      flux_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case SMOOTH:
      smooth_norm_ = param_10bit_to_f32(value);
      break;
    case SEED:
      seed_ = static_cast<uint32_t>(value) + 1U;
      rng_ = seed_;
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *allocated_buffer) override final
  {
    float *cursor = allocated_buffer;
    analysis_ = cursor;
    cursor += kFftSize;
    ola_ = cursor;
    cursor += kFftSize;
    fft_re_ = cursor;
    cursor += kFftSize;
    fft_im_ = cursor;
    cursor += kFftSize;
    mag_ = cursor;
    cursor += kNumBins;
    mag_prev_ = cursor;
    cursor += kNumBins;
    band_gain_ = cursor;
    cursor += kMaxBands;
    band_target_ = cursor;

    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;

    fifo_count_ = 0U;
    ola_read_pos_ = 0U;
    analysis_write_pos_ = 0U;
    reroll_all_ = true;
    rng_ = seed_;
    for (uint32_t bandIndex = 0; bandIndex < kMaxBands; ++bandIndex)
    {
      band_gain_[bandIndex] = 1.f;
      band_target_[bandIndex] = 1.f;
    }
  }

  void teardown() override final
  {
    analysis_ = nullptr;
    ola_ = nullptr;
    fft_re_ = nullptr;
    fft_im_ = nullptr;
    mag_ = nullptr;
    mag_prev_ = nullptr;
    band_gain_ = nullptr;
    band_target_ = nullptr;
  }

  void reset() override final
  {
    if (analysis_ == nullptr)
      return;
    for (uint32_t sampleIndex = 0; sampleIndex < kFftSize; ++sampleIndex)
    {
      analysis_[sampleIndex] = 0.f;
      ola_[sampleIndex] = 0.f;
    }
    fifo_count_ = 0U;
    reroll_all_ = true;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began)
      reroll_all_ = true;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    for (uint32_t frameIndex = 0; frameIndex < frames; ++frameIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in + frameIndex * 2U, raw == nullptr ? nullptr : raw + frameIndex * 2U, live_left, live_right);
      const float mono_in = 0.5f * (live_left + live_right);

      analysis_[analysis_write_pos_] = mono_in;
      analysis_write_pos_ = (analysis_write_pos_ + 1U) & (kFftSize - 1U);
      ++fifo_count_;
      if (fifo_count_ >= kHopSize)
      {
        fifo_count_ = 0U;
        processHop();
      }

      const float wet = ola_[ola_read_pos_];
      ola_[ola_read_pos_] = 0.f;
      ola_read_pos_ = (ola_read_pos_ + 1U) & (kFftSize - 1U);

      out[frameIndex * 2U] = fx::mix(live_left, wet, mix_);
      out[frameIndex * 2U + 1U] = fx::mix(live_right, wet, mix_);
    }
  }

private:
  uint32_t bandCount() const
  {
    uint32_t count = 4U + static_cast<uint32_t>(bands_norm_ * 28.f);
    if (count > kMaxBands)
      count = kMaxBands;
    return count;
  }

  void rerollBand(uint32_t bandIndex)
  {
    const float roll = fx::randomFloat(rng_);
    band_target_[bandIndex] = 0.05f + roll * roll * 2.4f;
  }

  void processHop()
  {
    const uint32_t bands = bandCount();
    const float glide = 0.04f + (1.f - smooth_norm_) * 0.45f;

    if (reroll_all_)
    {
      for (uint32_t bandIndex = 0; bandIndex < bands; ++bandIndex)
        rerollBand(bandIndex);
      reroll_all_ = false;
    }
    else
    {
      for (uint32_t bandIndex = 0; bandIndex < bands; ++bandIndex)
      {
        if (fx::randomFloat(rng_) < flux_norm_ * flux_norm_)
          rerollBand(bandIndex);
      }
    }

    for (uint32_t bandIndex = 0; bandIndex < bands; ++bandIndex)
      band_gain_[bandIndex] += (band_target_[bandIndex] - band_gain_[bandIndex]) * glide;

    for (uint32_t sampleIndex = 0; sampleIndex < kFftSize; ++sampleIndex)
    {
      const uint32_t analysisIndex = (analysis_write_pos_ + sampleIndex) & (kFftSize - 1U);
      fft_re_[sampleIndex] = analysis_[analysisIndex] * kHannWindow[sampleIndex];
      fft_im_[sampleIndex] = 0.f;
    }

    spectralFft512(fft_re_, fft_im_);

    const float log_max = fasterlog2f(static_cast<float>(kNumBins - 1U));
    for (uint32_t binIndex = 0; binIndex < kNumBins; ++binIndex)
    {
      float gain = 1.f;
      if (binIndex > 0U)
      {
        const float coord = fasterlog2f(static_cast<float>(binIndex)) / log_max;
        float band_pos = coord * static_cast<float>(bands);
        if (band_pos >= static_cast<float>(bands))
          band_pos = static_cast<float>(bands) - 0.001f;
        const uint32_t lower = static_cast<uint32_t>(band_pos);
        const uint32_t upper = (lower + 1U < bands) ? lower + 1U : lower;
        const float frac = band_pos - static_cast<float>(lower);
        gain = band_gain_[lower] + (band_gain_[upper] - band_gain_[lower]) * frac;
      }
      fft_re_[binIndex] *= gain;
      fft_im_[binIndex] *= gain;
    }

    fft_im_[0] = 0.f;
    fft_im_[kNumBins - 1U] = 0.f;
    for (uint32_t binIndex = 1U; binIndex < kNumBins - 1U; ++binIndex)
    {
      fft_re_[kFftSize - binIndex] = fft_re_[binIndex];
      fft_im_[kFftSize - binIndex] = -fft_im_[binIndex];
    }

    spectralIfft512(fft_re_, fft_im_);

    const uint32_t write_start = ola_read_pos_;
    for (uint32_t sampleIndex = 0; sampleIndex < kFftSize; ++sampleIndex)
    {
      const uint32_t olaIndex = (write_start + sampleIndex) & (kFftSize - 1U);
      ola_[olaIndex] += fft_re_[sampleIndex] * kHannWindow[sampleIndex] * kOlaGain;
    }
  }

  float *analysis_ = nullptr;
  float *ola_ = nullptr;
  float *fft_re_ = nullptr;
  float *fft_im_ = nullptr;
  float *mag_ = nullptr;
  float *mag_prev_ = nullptr;
  float *band_gain_ = nullptr;
  float *band_target_ = nullptr;
  uint32_t fifo_count_ = 0U;
  uint32_t ola_read_pos_ = 0U;
  uint32_t analysis_write_pos_ = 0U;
  uint32_t seed_ = 2U;
  uint32_t rng_ = 2U;
  float bands_norm_ = 0.5f;
  float flux_norm_ = 0.37f;
  float smooth_norm_ = 0.5f;
  float mix_ = 1.f;
  bool reroll_all_ = true;
};
