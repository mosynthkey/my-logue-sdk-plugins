#include "hhat.h"
#include "macros.h"
#include "runtime.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static void render(HHat &hat, std::vector<float> &mono, uint32_t frames)
{
  std::vector<float> block(frames * 2U, 0.f);
  hat.process(block.data(), block.data(), frames);
  for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    mono.push_back(block[sampleIndex * 2U]);
}

static float peakAbs(const std::vector<float> &mono)
{
  float peak = 0.f;
  for (float sample : mono)
  {
    const float magnitude = std::fabs(sample);
    if (magnitude > peak)
      peak = magnitude;
  }
  return peak;
}

static float rms(const std::vector<float> &mono)
{
  double sum_squares = 0.0;
  for (float sample : mono)
    sum_squares += static_cast<double>(sample) * static_cast<double>(sample);
  if (mono.empty())
    return 0.f;
  return static_cast<float>(std::sqrt(sum_squares / static_cast<double>(mono.size())));
}

static float energyAfter(const std::vector<float> &mono, uint32_t start, uint32_t length)
{
  double sum_squares = 0.0;
  uint32_t count = 0U;
  for (uint32_t sampleIndex = start; sampleIndex < mono.size() && count < length; ++sampleIndex, ++count)
  {
    const double sample = static_cast<double>(mono[sampleIndex]);
    sum_squares += sample * sample;
  }
  if (count == 0U)
    return 0.f;
  return static_cast<float>(std::sqrt(sum_squares / static_cast<double>(count)));
}

static uint32_t countAttacks(const std::vector<float> &mono, float threshold)
{
  uint32_t attacks = 0U;
  uint32_t refractory = 0U;
  for (float sample : mono)
  {
    if (refractory > 0U)
    {
      --refractory;
      continue;
    }
    if (std::fabs(sample) > threshold)
    {
      ++attacks;
      refractory = 1200U;
    }
  }
  return attacks;
}

static void setup(HHat &hat, int32_t dens, int32_t open)
{
  hat.init(nullptr);
  hat.setParameter(HHat::MIX, 1000);
  hat.setParameter(HHat::DENS, dens);
  hat.setParameter(HHat::OPEN, open);
  hat.setParameter(HHat::TONE, 563);
  hat.setParameter(HHat::TUNE, 512);
  hat.setParameter(HHat::DEC, 512);
  hat.setTempo(120.f);
}

int main()
{
  HHat hat;
  setup(hat, 480, 0);

  // Mid density ≈ 8 hits on a 16-step Euclidean grid starting at step 0.
  if (hat.debugHits() != 8U)
  {
    std::printf("mid dens hits=%u\n", hat.debugHits());
    return 1;
  }
  if (!hat.debugStepHit(0U) || !hat.debugStepHit(2U) || hat.debugStepHit(1U))
  {
    std::printf("even-16th euclid failed\n");
    return 2;
  }

  setup(hat, 70, 0);
  if (hat.debugHits() != 2U)
  {
    std::printf("low dens hits=%u\n", hat.debugHits());
    return 3;
  }

  setup(hat, 1023, 0);
  if (hat.debugHits() != 16U)
  {
    std::printf("high dens hits=%u\n", hat.debugHits());
    return 4;
  }

  // Closed body should still be alive past ~20 ms (not an HSnare-style click).
  setup(hat, 480, 0);
  hat.debugTrigger(1.f);
  std::vector<float> closed;
  render(hat, closed, 48000U / 5U);
  const float closed_peak = peakAbs(closed);
  const float closed_body = energyAfter(closed, 960U, 960U); // ~20–40 ms
  std::printf("closed_peak=%.4f closed_body_rms=%.5f tau=%.4f\n", closed_peak, closed_body,
              hat.debugTauSeconds());
  if (closed_peak < 0.04f || closed_peak > 0.99f)
    return 5;
  if (closed_body < 0.008f)
    return 6;

  // Open should ring longer than closed.
  setup(hat, 480, 1023);
  hat.debugTrigger(1.f);
  std::vector<float> open;
  render(hat, open, 48000U / 2U);
  const float open_late = energyAfter(open, 9600U, 2400U); // ~200–250 ms
  setup(hat, 480, 0);
  hat.debugTrigger(1.f);
  std::vector<float> closed_long;
  render(hat, closed_long, 48000U / 2U);
  const float closed_late = energyAfter(closed_long, 9600U, 2400U);
  std::printf("open_late=%.5f closed_late=%.5f\n", open_late, closed_late);
  if (open_late <= closed_late * 1.5f)
    return 7;

  // Half-open sits between closed and open decay lengths.
  setup(hat, 480, 512);
  const float half_tau = hat.debugTauSeconds();
  setup(hat, 480, 0);
  const float closed_tau = hat.debugTauSeconds();
  setup(hat, 480, 1023);
  const float open_tau = hat.debugTauSeconds();
  std::printf("tau closed=%.4f half=%.4f open=%.4f\n", closed_tau, half_tau, open_tau);
  if (!(closed_tau < half_tau && half_tau < open_tau))
    return 8;

  // Phrase density scales with X.
  setup(hat, 70, 0);
  hat.touchEvent(0, k_unit_touch_phase_began, 70, 0);
  std::vector<float> bar_sparse;
  render(hat, bar_sparse, 48000U * 2U);
  const uint32_t attacks_low = countAttacks(bar_sparse, 0.03f);
  setup(hat, 1023, 0);
  hat.touchEvent(0, k_unit_touch_phase_began, 1023, 0);
  std::vector<float> bar_busy;
  render(hat, bar_busy, 48000U * 2U);
  const uint32_t attacks_high = countAttacks(bar_busy, 0.025f);
  std::printf("low_dens_attacks=%u high_dens_attacks=%u\n", attacks_low, attacks_high);
  if (attacks_low < 2U || attacks_low > 8U)
    return 9;
  if (attacks_high <= attacks_low + 6U)
    return 10;

  if (hat.debugTriggerCount() == 0U)
    return 11;

  std::printf("rms_closed=%.5f rms_open=%.5f\n", rms(closed), rms(open));
  std::printf("ok\n");
  return 0;
}
