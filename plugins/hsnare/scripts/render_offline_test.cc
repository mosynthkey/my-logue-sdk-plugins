#include "hsnare.h"
#include "macros.h"
#include "runtime.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static void render(HSnare &snare, std::vector<float> &mono, uint32_t frames)
{
  std::vector<float> block(frames * 2U, 0.f);
  snare.process(block.data(), block.data(), frames);
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

static float bandEnergy(const std::vector<float> &mono, float hz_lo, float hz_hi)
{
  const uint32_t n = 4096U;
  if (mono.size() < n)
    return 0.f;
  double energy = 0.0;
  const uint32_t bin_lo = static_cast<uint32_t>(hz_lo * static_cast<float>(n) / 48000.f);
  const uint32_t bin_hi = static_cast<uint32_t>(hz_hi * static_cast<float>(n) / 48000.f);
  for (uint32_t binIndex = bin_lo; binIndex <= bin_hi && binIndex < n / 2U; ++binIndex)
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
    energy += real * real + imag * imag;
  }
  return static_cast<float>(energy);
}

static void setup(HSnare &snare, int32_t dens, int32_t type)
{
  snare.init(nullptr);
  snare.setParameter(HSnare::MIX, 1000);
  snare.setParameter(HSnare::DENS, dens);
  snare.setParameter(HSnare::TYPE, type);
  snare.setParameter(HSnare::TONE, 430);
  snare.setParameter(HSnare::SNAP, 400);
  snare.setParameter(HSnare::TUNE, 512);
  snare.setTempo(120.f);
}

int main()
{
  HSnare snare;
  setup(snare, 70, 0);

  if (snare.debugHits() != 2U)
  {
    std::printf("low dens hits=%u\n", snare.debugHits());
    return 1;
  }
  if (!snare.debugStepHit(4U) || !snare.debugStepHit(12U) || snare.debugStepHit(0U))
  {
    std::printf("2-and-4 rotation failed\n");
    return 2;
  }

  setup(snare, 1023, 0);
  if (snare.debugHits() != 16U)
  {
    std::printf("high dens hits=%u\n", snare.debugHits());
    return 3;
  }

  setup(snare, 70, 0);
  snare.debugTrigger(1.f);
  std::vector<float> voice_808;
  render(snare, voice_808, 8192U);
  const float peak_808 = peakAbs(voice_808);
  const float shell_808 = bandEnergy(voice_808, 140.f, 220.f);
  const float harm_808 = bandEnergy(voice_808, 280.f, 400.f);
  const float hiss_808 = bandEnergy(voice_808, 4000.f, 10000.f);
  std::printf("808 peak=%.4f shell=%.3f harm=%.3f hiss=%.3f\n", peak_808, shell_808, harm_808, hiss_808);
  if (peak_808 < 0.04f || peak_808 > 0.99f)
    return 4;
  if (shell_808 < 1.0f && harm_808 < 1.0f)
    return 5;

  setup(snare, 70, 1023);
  snare.debugTrigger(1.f);
  std::vector<float> voice_909;
  render(snare, voice_909, 8192U);
  const float peak_909 = peakAbs(voice_909);
  const float hiss_909 = bandEnergy(voice_909, 4000.f, 10000.f);
  std::printf("909 peak=%.4f hiss=%.3f\n", peak_909, hiss_909);
  if (peak_909 < 0.04f)
    return 6;
  if (hiss_909 + 1.0f < hiss_808 * 0.35f)
    return 7;

  setup(snare, 70, 0);
  snare.touchEvent(0, k_unit_touch_phase_began, 70, 0);
  std::vector<float> bar_low;
  render(snare, bar_low, 48000U * 2U);
  const uint32_t attacks_low = countAttacks(bar_low, 0.03f);
  std::printf("low_dens_attacks=%u\n", attacks_low);
  if (attacks_low < 2U || attacks_low > 6U)
    return 8;

  setup(snare, 1023, 0);
  snare.touchEvent(0, k_unit_touch_phase_began, 1023, 0);
  std::vector<float> bar_busy;
  render(snare, bar_busy, 48000U * 2U);
  const uint32_t attacks_high = countAttacks(bar_busy, 0.025f);
  std::printf("high_dens_attacks=%u\n", attacks_high);
  if (attacks_high <= attacks_low + 4U)
    return 9;

  std::printf("ok\n");
  return 0;
}
