#include "mohowl_engine.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

static float renderPeak(MoHowlEngine &engine, float sample_rate, uint32_t frames)
{
  float peak = 0.f;
  for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
  {
    const float sample = engine.render(sample_rate);
    const float abs_sample = sample < 0.f ? -sample : sample;
    if (abs_sample > peak)
      peak = abs_sample;
  }
  return peak;
}

static uint32_t zeroCrossingSpread(MoHowlEngine &engine, float sample_rate)
{
  const uint32_t window_frames = 4800U;
  const uint32_t window_count = 10U;
  uint32_t min_crossings = 0xFFFFFFFFU;
  uint32_t max_crossings = 0U;
  float previous = engine.render(sample_rate);

  for (uint32_t windowIndex = 0; windowIndex < window_count; ++windowIndex)
  {
    uint32_t crossings = 0U;
    for (uint32_t sampleIndex = 0; sampleIndex < window_frames; ++sampleIndex)
    {
      const float sample = engine.render(sample_rate);
      if ((previous < 0.f && sample >= 0.f) || (previous > 0.f && sample <= 0.f))
        ++crossings;
      previous = sample;
    }
    if (crossings < min_crossings)
      min_crossings = crossings;
    if (crossings > max_crossings)
      max_crossings = crossings;
  }

  return max_crossings - min_crossings;
}

int main()
{
  const float sample_rate = 48000.f;
  MoHowlEngine engine;
  engine.init();

  MoHowlEngine::Params params;
  params.lfo_depth = 0.f;
  params.harmonics = 0.5f;
  params.pitch = 0.5f;
  params.lfo_rate = 0.7f;
  params.decay = 0.f;
  params.level = 1.f;
  engine.setParams(params);

  const float silent = renderPeak(engine, sample_rate, 2048U);
  if (silent > 1.0e-4f)
  {
    std::printf("FAIL: idle peak %.6f\n", silent);
    return 1;
  }

  engine.gate(true);
  const float held = renderPeak(engine, sample_rate, 48000U);
  if (held < 0.08f || held > 0.85f)
  {
    std::printf("FAIL: held peak %.6f\n", held);
    return 1;
  }

  params.lfo_depth = 0.f;
  engine.setParams(params);
  engine.gate(true);
  const uint32_t steady_spread = zeroCrossingSpread(engine, sample_rate);

  params.lfo_depth = 1.f;
  engine.setParams(params);
  engine.gate(true);
  const uint32_t wobble_spread = zeroCrossingSpread(engine, sample_rate);
  if (wobble_spread <= steady_spread + 8U)
  {
    std::printf("FAIL: LFO depth did not move pitch (steady=%u wobble=%u)\n",
                steady_spread, wobble_spread);
    return 1;
  }

  engine.gate(false);
  float release_peak = 0.f;
  uint32_t quiet_after = 0U;
  for (uint32_t sampleIndex = 0; sampleIndex < 48000U * 2U; ++sampleIndex)
  {
    const float sample = engine.render(sample_rate);
    const float abs_sample = sample < 0.f ? -sample : sample;
    if (abs_sample > release_peak)
      release_peak = abs_sample;
    if (abs_sample < 1.0e-3f)
      ++quiet_after;
    else
      quiet_after = 0U;
  }

  if (release_peak < 0.01f)
  {
    std::printf("FAIL: release was silent immediately (%.6f)\n", release_peak);
    return 1;
  }
  if (quiet_after < 256U)
  {
    std::printf("FAIL: release did not settle\n");
    return 1;
  }

  std::printf("ok idle=%.6f held=%.6f release=%.6f spread_steady=%u spread_wobble=%u\n",
              silent, held, release_peak, steady_spread, wobble_spread);
  return 0;
}
