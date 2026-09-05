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

int main()
{
  const float sample_rate = 48000.f;
  MoHowlEngine engine;
  engine.init();

  MoHowlEngine::Params params;
  params.pitch = 0.5f;
  params.feedback = 1.f;
  params.harmonics = 0.5f;
  params.swoop = 0.6f;
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

  std::printf("ok idle=%.6f held=%.6f release=%.6f\n", silent, held, release_peak);
  return 0;
}
