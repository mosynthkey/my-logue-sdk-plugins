#include "airhorn_engine.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

static bool approxEqual(float a, float b, float tol)
{
  return std::fabs(a - b) <= tol;
}

int main()
{
  AirHornEngine engine;
  engine.init();
  engine.setTrackFromParam(false);
  engine.setParameter(AirHornEngine::LEVEL, 1023);
  engine.setParameter(AirHornEngine::PMODE, 0); // Fixed

  engine.startVoice(127, 63); // D#4 — should match fixed pitch

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

  if (peak < 0.1f)
  {
    std::printf("FAIL: fixed sustain peak too low (%.6f)\n", peak);
    return 1;
  }

  engine.releaseAll();
  for (uint32_t sampleIndex = 0; sampleIndex < 48000U / 5U; ++sampleIndex)
    (void)engine.renderMono();

  const float expected = AirHornEngine::noteTransposeFor(60);
  const float want = std::pow(2.f, (60.f - 63.f) / 12.f);
  if (!approxEqual(expected, want, 0.01f))
  {
    std::printf("FAIL: note transpose got %.6f want %.6f\n", expected, want);
    return 1;
  }

  AirHornEngine nts3;
  nts3.init();
  nts3.setTrackFromParam(true);
  nts3.setParameter(AirHornEngine::LEVEL, 1023);
  nts3.setParameter(AirHornEngine::DECAY, 127);
  nts3.setParameter(AirHornEngine::MIX, 1000);
  nts3.setParameter(AirHornEngine::PMODE_NTS3, 1);
  nts3.setParameter(AirHornEngine::PITCH, 12); // +1 oct

  const char *sustain = nts3.getParameterStrValue(AirHornEngine::DECAY, 127);
  if (sustain == nullptr || std::strcmp(sustain, "Sustain") != 0)
  {
    std::printf("FAIL: expected Sustain label at decay 127\n");
    return 1;
  }
  const char *pitch_mode = nts3.getParameterStrValue(AirHornEngine::PMODE_NTS3, 1);
  if (pitch_mode == nullptr || std::strcmp(pitch_mode, "Pitch") != 0)
  {
    std::printf("FAIL: expected Pitch mode label\n");
    return 1;
  }
  if (!approxEqual(nts3.pitchTranspose(), 2.f, 0.02f))
  {
    std::printf("FAIL: +12 semis transpose got %.6f\n", nts3.pitchTranspose());
    return 1;
  }

  AirHornEngine fade_engine;
  fade_engine.init();
  fade_engine.setTrackFromParam(true);
  fade_engine.setParameter(AirHornEngine::LEVEL, 1023);
  fade_engine.setParameter(AirHornEngine::DECAY, 0);
  fade_engine.setParameter(AirHornEngine::MIX, 1000);
  fade_engine.startVoice(127, 0);
  float late_peak = 0.f;
  for (uint32_t sampleIndex = 0; sampleIndex < 48000U * 3U; ++sampleIndex)
  {
    const float sample = fade_engine.renderMono();
    if (sampleIndex > 48000U * 2U)
    {
      const float magnitude = std::fabs(sample);
      if (magnitude > late_peak)
        late_peak = magnitude;
    }
  }
  if (late_peak > 0.02f)
  {
    std::printf("FAIL: short decay still loud at 2-3s (%.6f)\n", late_peak);
    return 1;
  }

  const double rms = std::sqrt(sum_squares / static_cast<double>(sample_count));
  std::printf("peak=%.6f rms=%.6f max_delta=%.6f loop_samples=%u key_xpose=%.4f\n",
              peak, rms, max_delta, kAirhornSamples[0].length, expected);
  if (rms < 0.02f)
    return 1;
  return 0;
}
