#include "airhorn_engine.h"

#include <cmath>
#include <cstdio>
#include <cstdint>

int main()
{
  AirHornEngine engine;
  engine.init();
  engine.setParameter(AirHornEngine::LEVEL, 1023);
  engine.setParameter(AirHornEngine::MIX, 1000);
  engine.startVoice(127, 60);

  float peak = 0.f;
  double sum_squares = 0.0;
  uint64_t sample_count = 0U;
  float prev = 0.f;
  float max_delta = 0.f;

  for (uint32_t sampleIndex = 0; sampleIndex < 48000U * 2U; ++sampleIndex)
  {
    const float sample = engine.renderMono();
    const float magnitude = std::fabs(sample);
    if (magnitude > peak)
      peak = magnitude;
    const float delta = std::fabs(sample - prev);
    if (sampleIndex > 64U && delta > max_delta)
      max_delta = delta;
    sum_squares += static_cast<double>(sample) * static_cast<double>(sample);
    ++sample_count;
    prev = sample;
  }

  engine.releaseAll();
  for (uint32_t sampleIndex = 0; sampleIndex < 48000U / 5U; ++sampleIndex)
    (void)engine.renderMono();

  const double rms = std::sqrt(sum_squares / static_cast<double>(sample_count));
  std::printf("peak=%.6f rms=%.6f max_delta=%.6f loop_samples=%u\n",
              peak, rms, max_delta, kAirhornSamples[0].length);
  if (peak < 0.1f || rms < 0.02f)
    return 1;
  return 0;
}
