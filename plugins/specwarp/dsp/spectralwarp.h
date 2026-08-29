#pragma once

/*
 * File: spectralwarp.h
 *
 * Spectral Stretch / Smear FX for NTS-1 mkII and NTS-3.
 * Input -> FFT -> spectral warp -> IFFT with overlap-add.
 * X = Stretch (frequency-axis warp), Y = Smear (magnitude blur + phase diffusion).
 */

#include "macros.h"
#include "processor.h"
#include "spectralfft.h"
#include <stdint.h>

class SpectralWarp : public Processor
{
public:
  static constexpr uint32_t kFftSize = 512U;
  static constexpr uint32_t kHopSize = 128U;
  static constexpr uint32_t kNumBins = kFftSize / 2U + 1U;
  static constexpr float kOlaGain = 0.42f;
  static constexpr float kMagFloor = 1e-10f;

  uint32_t getBufferSize() const override final
  {
    return kFftSize * 4U + kNumBins * 3U;
  }

  enum
  {
    STRETCH = 0U,
    SMEAR,
    MIX,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case STRETCH:
      stretch_ = param_10bit_to_f32(value);
      break;
    case SMEAR:
      smear_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = value / 1000.f;
      if (mix_ < 0.f)
        mix_ = 0.f;
      if (mix_ > 1.f)
        mix_ = 1.f;
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

    mag_warp_ = cursor;

    stretch_ = 0.f;
    smear_ = 0.f;
    mix_ = 1.f;
    fifo_count_ = 0U;
    ola_read_pos_ = 0U;

    clearBuffers();
  }

  void teardown() override final
  {
    analysis_ = nullptr;
    ola_ = nullptr;
    fft_re_ = nullptr;
    fft_im_ = nullptr;
    mag_ = nullptr;
    mag_prev_ = nullptr;
    mag_warp_ = nullptr;
  }

  void reset() override final { clearBuffers(); }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    const float stretch = stretch_;
    const float smear = smear_;
    const float wetGain = mix_;
    const float dryGain = 1.f - wetGain;

    for (uint32_t frameIndex = 0; frameIndex < frames; ++frameIndex)
    {
      const float dryLeft = in[frameIndex * 2U];
      const float dryRight = in[frameIndex * 2U + 1U];
      const float monoIn = 0.5f * (dryLeft + dryRight);

      pushSample(monoIn);

      if (fifo_count_ >= kHopSize)
      {
        fifo_count_ = 0U;
        processHop(stretch, smear);
      }

      const float wet = ola_[ola_read_pos_];
      ola_[ola_read_pos_] = 0.f;
      ola_read_pos_ = (ola_read_pos_ + 1U) & (kFftSize - 1U);

      const float processed = dryGain * monoIn + wetGain * wet;
      out[frameIndex * 2U] = dryGain * dryLeft + wetGain * processed;
      out[frameIndex * 2U + 1U] = dryGain * dryRight + wetGain * processed;
    }
  }

private:
  float *analysis_;
  float *ola_;
  float *fft_re_;
  float *fft_im_;
  float *mag_;
  float *mag_prev_;
  float *mag_warp_;

  float stretch_;
  float smear_;
  float mix_;
  uint32_t fifo_count_;
  uint32_t ola_read_pos_;

  void clearBuffers()
  {
    if (!analysis_)
      return;

    for (uint32_t sampleIndex = 0; sampleIndex < kFftSize; ++sampleIndex)
    {
      analysis_[sampleIndex] = 0.f;
      ola_[sampleIndex] = 0.f;
    }

    for (uint32_t binIndex = 0; binIndex < kNumBins; ++binIndex)
      mag_prev_[binIndex] = 0.f;
  }

  void pushSample(float sample)
  {
    for (uint32_t sampleIndex = 0; sampleIndex < kFftSize - 1U; ++sampleIndex)
      analysis_[sampleIndex] = analysis_[sampleIndex + 1U];

    analysis_[kFftSize - 1U] = sample;
    ++fifo_count_;
  }

  static float lerp1D(const float *values, float position, uint32_t maxIndex)
  {
    if (position <= 0.f)
      return values[0];

    if (position >= static_cast<float>(maxIndex))
      return values[maxIndex];

    const uint32_t lowerIndex = static_cast<uint32_t>(position);
    const float fraction = position - static_cast<float>(lowerIndex);
    return values[lowerIndex] + fraction * (values[lowerIndex + 1U] - values[lowerIndex]);
  }

  static float pseudoPhase(uint32_t binIndex, float smearAmount)
  {
    const uint32_t hash = binIndex * 2654435761U;
    const float normalized = static_cast<float>(hash & 0xFFFFU) * (1.f / 65535.f);
    return (normalized - 0.5f) * smearAmount * 1.2f;
  }

  static float fastSqrt(float value)
  {
    if (value <= 0.f)
      return 0.f;

    float estimate = value;
    estimate = 0.5f * (estimate + value / estimate);
    estimate = 0.5f * (estimate + value / estimate);
    return estimate;
  }

  void processHop(float stretch, float smear)
  {
    const float stretchFactor = 0.2f + stretch * stretch * 7.8f;
    const float smearAmount = smear * smear;
    const uint32_t maxBin = kNumBins - 1U;
    const uint32_t blurRadius = static_cast<uint32_t>(smearAmount * 48.f);

    for (uint32_t sampleIndex = 0; sampleIndex < kFftSize; ++sampleIndex)
    {
      const float window = kHannWindow[sampleIndex];
      fft_re_[sampleIndex] = analysis_[sampleIndex] * window;
      fft_im_[sampleIndex] = 0.f;
    }

    spectralFft512(fft_re_, fft_im_);

    for (uint32_t binIndex = 0; binIndex < kNumBins; ++binIndex)
    {
      const float binRe = fft_re_[binIndex];
      const float binIm = fft_im_[binIndex];
      mag_[binIndex] = binRe * binRe + binIm * binIm + kMagFloor;
    }

    if (blurRadius > 0U)
    {
      for (uint32_t binIndex = 0; binIndex < kNumBins; ++binIndex)
      {
        const uint32_t startBin = (binIndex > blurRadius) ? binIndex - blurRadius : 0U;
        const uint32_t endBin = (binIndex + blurRadius < maxBin) ? binIndex + blurRadius : maxBin;

        float sum = 0.f;
        for (uint32_t blurIndex = startBin; blurIndex <= endBin; ++blurIndex)
          sum += mag_[blurIndex];

        mag_warp_[binIndex] = sum / static_cast<float>(endBin - startBin + 1U);
      }

      for (uint32_t binIndex = 0; binIndex < kNumBins; ++binIndex)
        mag_[binIndex] = mag_warp_[binIndex];
    }

    if (smearAmount > 0.01f)
    {
      const float temporalBlend = smearAmount * 0.75f;
      const float invTemporal = 1.f - temporalBlend;
      for (uint32_t binIndex = 0; binIndex < kNumBins; ++binIndex)
        mag_[binIndex] = mag_[binIndex] * invTemporal + mag_prev_[binIndex] * temporalBlend;
    }

    for (uint32_t binIndex = 0; binIndex < kNumBins; ++binIndex)
    {
      const float sourcePos = static_cast<float>(binIndex) / stretchFactor;
      mag_warp_[binIndex] = lerp1D(mag_, sourcePos, maxBin);
    }

    for (uint32_t binIndex = 0; binIndex < kNumBins; ++binIndex)
    {
      const float sourcePos = static_cast<float>(binIndex) / stretchFactor;
      const float sourceRe = lerp1D(fft_re_, sourcePos, maxBin);
      const float sourceIm = lerp1D(fft_im_, sourcePos, maxBin);
      const float sourceMag = sourceRe * sourceRe + sourceIm * sourceIm + kMagFloor;
      const float targetMag = mag_warp_[binIndex];
      const float gain = fastSqrt(targetMag / sourceMag);

      float binRe = sourceRe * gain;
      float binIm = sourceIm * gain;

      const float diffuseAngle = pseudoPhase(binIndex, smearAmount);
      const float rotatedRe = binRe - binIm * diffuseAngle;
      const float rotatedIm = binIm + binRe * diffuseAngle;
      binRe = rotatedRe;
      binIm = rotatedIm;

      fft_re_[binIndex] = binRe;
      fft_im_[binIndex] = binIm;
      mag_prev_[binIndex] = mag_[binIndex];
    }

    for (uint32_t binIndex = 1U; binIndex < kNumBins - 1U; ++binIndex)
    {
      fft_re_[kFftSize - binIndex] = fft_re_[binIndex];
      fft_im_[kFftSize - binIndex] = -fft_im_[binIndex];
    }

    spectralIfft512(fft_re_, fft_im_);

    const uint32_t writeStart = ola_read_pos_;
    for (uint32_t sampleIndex = 0; sampleIndex < kFftSize; ++sampleIndex)
    {
      const uint32_t olaIndex = (writeStart + sampleIndex) & (kFftSize - 1U);
      const float window = kHannWindow[sampleIndex];
      ola_[olaIndex] += fft_re_[sampleIndex] * window * kOlaGain;
    }
  }
};
