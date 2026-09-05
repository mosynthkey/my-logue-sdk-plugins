#pragma once

/*
 * File: spectralfft.h
 *
 * Compact in-place 512-point complex FFT for spectral FX.
 * No libm dependency; twiddle factors and window are precomputed.
 */

#include "spectralfft_tables.h"
#include <stdint.h>

static inline void spectralFft512(float *re, float *im)
{
  for (uint32_t sampleIndex = 0; sampleIndex < 512U; ++sampleIndex)
  {
    const uint32_t revIndex = kBitRev512[sampleIndex];
    if (revIndex > sampleIndex)
    {
      const float tmpRe = re[sampleIndex];
      re[sampleIndex] = re[revIndex];
      re[revIndex] = tmpRe;

      const float tmpIm = im[sampleIndex];
      im[sampleIndex] = im[revIndex];
      im[revIndex] = tmpIm;
    }
  }

  for (uint32_t stageSize = 2U; stageSize <= 512U; stageSize <<= 1U)
  {
    const uint32_t halfStage = stageSize >> 1U;
    const uint32_t twiddleStep = 512U / stageSize;

    for (uint32_t groupStart = 0U; groupStart < 512U; groupStart += stageSize)
    {
      for (uint32_t butterflyIndex = 0U; butterflyIndex < halfStage; ++butterflyIndex)
      {
        const uint32_t twiddleIndex = butterflyIndex * twiddleStep;
        const float twRe = kTwiddleRe256[twiddleIndex];
        const float twIm = kTwiddleIm256[twiddleIndex];

        const uint32_t evenIndex = groupStart + butterflyIndex;
        const uint32_t oddIndex = evenIndex + halfStage;

        const float oddRe = re[oddIndex];
        const float oddIm = im[oddIndex];

        const float prodRe = twRe * oddRe - twIm * oddIm;
        const float prodIm = twRe * oddIm + twIm * oddRe;

        re[oddIndex] = re[evenIndex] - prodRe;
        im[oddIndex] = im[evenIndex] - prodIm;
        re[evenIndex] = re[evenIndex] + prodRe;
        im[evenIndex] = im[evenIndex] + prodIm;
      }
    }
  }
}

static inline void spectralIfft512(float *re, float *im)
{
  for (uint32_t sampleIndex = 0; sampleIndex < 512U; ++sampleIndex)
    im[sampleIndex] = -im[sampleIndex];

  spectralFft512(re, im);

  const float scale = 1.f / 512.f;
  for (uint32_t sampleIndex = 0; sampleIndex < 512U; ++sampleIndex)
  {
    re[sampleIndex] *= scale;
    im[sampleIndex] = -im[sampleIndex] * scale;
  }
}
