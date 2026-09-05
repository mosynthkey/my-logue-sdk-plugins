#define FBACKOSC_NO_OSC_API
#include "fbackosc_engine.h"

#include <cmath>
#include <cstdio>
#include <cstdint>

struct Stats
{
  float peak = 0.f;
  double sum_squares = 0.0;
  uint32_t count = 0U;
};

static void accumulate(Stats &stats, float sample)
{
  const float abs_sample = sample < 0.f ? -sample : sample;
  if (abs_sample > stats.peak)
    stats.peak = abs_sample;
  stats.sum_squares += static_cast<double>(sample) * static_cast<double>(sample);
  ++stats.count;
}

static float rms(const Stats &stats)
{
  if (stats.count == 0U)
    return 0.f;
  return static_cast<float>(std::sqrt(stats.sum_squares / static_cast<double>(stats.count)));
}

static Stats renderAt(float feedback, float harmonics, float w0, float note)
{
  FBackOscEngine engine;
  FBackOscEngine::Params params;
  params.harmonics = harmonics;
  params.feedback = feedback;
  engine.setParams(params);
  engine.setPitch(w0, note);
  engine.reset();

  for (uint32_t settleIndex = 0; settleIndex < 48000U; ++settleIndex)
    (void)engine.render();

  Stats stats;
  for (uint32_t sampleIndex = 0; sampleIndex < 48000U; ++sampleIndex)
    accumulate(stats, engine.render());
  return stats;
}

int main()
{
  const float note = 60.f;
  const float w0 = 261.625565f / 48000.f;
  const float feedbacks[] = {0.f, 0.45f, 0.75f, 1.f};
  bool ok = true;

  std::printf("feedback  peak     rms      peak_dB\n");
  float peak_at_zero = 0.f;
  float peak_at_full = 0.f;

  for (float feedback : feedbacks)
  {
    const Stats stats = renderAt(feedback, 0.5f, w0, note);
    const float peak_db = 20.f * std::log10(stats.peak + 1.0e-12f);
    std::printf("%5.2f     %.4f   %.4f   %+6.2f\n", feedback, stats.peak, rms(stats), peak_db);

    if (feedback == 0.f)
      peak_at_zero = stats.peak;
    if (feedback == 1.f)
      peak_at_full = stats.peak;

    if (stats.peak > 0.85f)
    {
      std::printf("FAIL: peak %.4f exceeds 0.85 at feedback %.2f\n", stats.peak, feedback);
      ok = false;
    }
  }

  if (peak_at_zero < 0.15f)
  {
    std::printf("FAIL: zero-feedback peak %.4f is too quiet\n", peak_at_zero);
    ok = false;
  }

  const float rise_db = 20.f * std::log10((peak_at_full + 1.0e-12f) / (peak_at_zero + 1.0e-12f));
  std::printf("full-vs-zero peak rise = %+.2f dB\n", rise_db);
  if (rise_db > 8.f)
  {
    std::printf("FAIL: feedback still boosts level by %.2f dB\n", rise_db);
    ok = false;
  }

  return ok ? 0 : 1;
}
