#include "hclap.h"
#include "macros.h"
#include "runtime.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static void render(HClap &clap, std::vector<float> &mono, uint32_t frames)
{
  std::vector<float> block(frames * 2U, 0.f);
  clap.process(block.data(), block.data(), frames);
  for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    mono.push_back(block[sampleIndex * 2U]);
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
      refractory = 2880U;
    }
  }
  return attacks;
}

static uint32_t countBurstPeaks(const std::vector<float> &mono, uint32_t start, uint32_t length)
{
  std::vector<float> env(length, 0.f);
  const uint32_t window = 48U;
  for (uint32_t sampleIndex = 0; sampleIndex < length; ++sampleIndex)
  {
    const uint32_t index = start + sampleIndex;
    if (index >= mono.size())
      break;
    float local = 0.f;
    for (uint32_t tapIndex = 0; tapIndex < window && index + tapIndex < mono.size(); ++tapIndex)
    {
      const float magnitude = std::fabs(mono[index + tapIndex]);
      if (magnitude > local)
        local = magnitude;
    }
    env[sampleIndex] = local;
  }

  uint32_t peaks = 0U;
  const uint32_t guard = 180U;
  for (uint32_t sampleIndex = guard; sampleIndex + guard < env.size(); ++sampleIndex)
  {
    const float center = env[sampleIndex];
    if (center < 0.01f)
      continue;
    bool is_peak = true;
    for (uint32_t neighborIndex = 1; neighborIndex <= guard; ++neighborIndex)
    {
      if (env[sampleIndex - neighborIndex] >= center || env[sampleIndex + neighborIndex] > center)
      {
        is_peak = false;
        break;
      }
    }
    if (is_peak)
      ++peaks;
  }
  return peaks;
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

static float spectralCentroid(const std::vector<float> &mono)
{
  const uint32_t n = 2048U;
  if (mono.size() < n)
    return 0.f;
  double weighted = 0.0;
  double total = 0.0;
  for (uint32_t binIndex = 1; binIndex < n / 2U; ++binIndex)
  {
    double real = 0.0;
    double imag = 0.0;
    const double omega = 6.283185307179586 * static_cast<double>(binIndex) / static_cast<double>(n);
    for (uint32_t sampleIndex = 0; sampleIndex < n; ++sampleIndex)
    {
      const double angle = omega * static_cast<double>(sampleIndex);
      const double sample = static_cast<double>(mono[sampleIndex]);
      real += sample * std::cos(angle);
      imag += sample * std::sin(angle);
    }
    const double mag = std::sqrt(real * real + imag * imag);
    weighted += mag * static_cast<double>(binIndex);
    total += mag;
  }
  if (total <= 0.0)
    return 0.f;
  return static_cast<float>(weighted / total * (48000.0 / static_cast<double>(n)));
}

static void setup(HClap &clap, int32_t dens, int32_t type)
{
  clap.init(nullptr);
  clap.setParameter(HClap::MIX, 1000);
  clap.setParameter(HClap::DENS, dens);
  clap.setParameter(HClap::TYPE, type);
  clap.setParameter(HClap::TONE, 512);
  clap.setParameter(HClap::DEC, 512);
  clap.setParameter(HClap::SNAP, 512);
  clap.setTempo(120.f);
}

int main()
{
  HClap clap;
  setup(clap, 70, 0);

  if (clap.debugHits() != 2U)
  {
    std::printf("low dens hits=%u\n", clap.debugHits());
    return 1;
  }
  if (!clap.debugStepHit(4U) || !clap.debugStepHit(12U) || clap.debugStepHit(0U))
  {
    std::printf("2-and-4 rotation failed\n");
    return 2;
  }

  setup(clap, 1023, 0);
  if (clap.debugHits() != 16U)
  {
    std::printf("high dens hits=%u\n", clap.debugHits());
    return 3;
  }

  setup(clap, 70, 0);
  clap.debugTrigger(1.f);
  std::vector<float> single;
  render(clap, single, 48000U / 8U);
  const uint32_t burst_peaks = countBurstPeaks(single, 0U, 2880U);
  const float single_peak = [&single]() {
    float peak = 0.f;
    for (float sample : single)
    {
      const float magnitude = std::fabs(sample);
      if (magnitude > peak)
        peak = magnitude;
    }
    return peak;
  }();
  std::printf("burst_peaks=%u single_peak=%.4f single_rms=%.5f\n", burst_peaks, single_peak, rms(single));
  if (burst_peaks < 3U || burst_peaks > 5U)
    return 4;
  if (single_peak < 0.04f || single_peak > 0.99f)
    return 5;

  setup(clap, 70, 0);
  clap.touchEvent(0, k_unit_touch_phase_began, 70, 0);
  std::vector<float> bar_808;
  render(clap, bar_808, 48000U * 2U);
  const uint32_t attacks_low = countAttacks(bar_808, 0.03f);
  std::printf("low_dens_attacks=%u\n", attacks_low);
  if (attacks_low < 2U || attacks_low > 6U)
    return 6;

  setup(clap, 1023, 0);
  clap.touchEvent(0, k_unit_touch_phase_began, 1023, 0);
  std::vector<float> bar_busy;
  render(clap, bar_busy, 48000U * 2U);
  const uint32_t attacks_high = countAttacks(bar_busy, 0.025f);
  std::printf("high_dens_attacks=%u\n", attacks_high);
  if (attacks_high <= attacks_low + 4U)
    return 7;

  setup(clap, 70, 0);
  clap.debugTrigger(1.f);
  std::vector<float> voice_808;
  render(clap, voice_808, 4096U);
  setup(clap, 70, 1023);
  clap.debugTrigger(1.f);
  std::vector<float> voice_909;
  render(clap, voice_909, 4096U);
  const float centroid_808 = spectralCentroid(voice_808);
  const float centroid_909 = spectralCentroid(voice_909);
  std::printf("centroid_808=%.1f centroid_909=%.1f\n", centroid_808, centroid_909);
  if (centroid_909 + 40.f < centroid_808)
    return 8;

  if (clap.debugTriggerCount() == 0U)
    return 9;

  std::printf("ok\n");
  return 0;
}
