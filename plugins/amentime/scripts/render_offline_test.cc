#include "amentime.h"
#include "macros.h"
#include "runtime.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static void setup(AmenTime &fx, float bpm, int32_t size_value)
{
  fx.init(nullptr);
  fx.setParameter(AmenTime::MIX, 1000);
  fx.setParameter(AmenTime::STRT, 0);
  fx.setParameter(AmenTime::SIZE, size_value);
  fx.setParameter(AmenTime::TUNE, 512);
  fx.setParameter(AmenTime::RPT, 0);
  fx.setParameter(AmenTime::REVS, 0);
  fx.setParameter(AmenTime::HOLD, 0);
  fx.setTempo(bpm);
}

static void render(AmenTime &fx, std::vector<float> &mono, uint32_t frames)
{
  std::vector<float> block(128U * 2U, 0.f);
  uint32_t remaining = frames;
  while (remaining > 0U)
  {
    const uint32_t block_frames = remaining > 128U ? 128U : remaining;
    fx.process(block.data(), block.data(), block_frames);
    for (uint32_t sampleIndex = 0; sampleIndex < block_frames; ++sampleIndex)
      mono.push_back(block[sampleIndex * 2U]);
    remaining -= block_frames;
  }
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
  if (mono.empty())
    return 0.f;
  double sum_squares = 0.0;
  for (float sample : mono)
    sum_squares += static_cast<double>(sample) * static_cast<double>(sample);
  return static_cast<float>(std::sqrt(sum_squares / static_cast<double>(mono.size())));
}

int main()
{
  AmenTime fx;
  setup(fx, 120.f, 640);
  if (fx.debugSlicesPerBar() != 16U)
  {
    std::printf("size 1/16 slices=%u\n", fx.debugSlicesPerBar());
    return 1;
  }

  setup(fx, 120.f, 100);
  if (fx.debugSlicesPerBar() != 4U)
  {
    std::printf("size 1/4 slices=%u\n", fx.debugSlicesPerBar());
    return 2;
  }

  setup(fx, 120.f, 900);
  if (fx.debugSlicesPerBar() != 32U)
  {
    std::printf("size 1/32 slices=%u\n", fx.debugSlicesPerBar());
    return 3;
  }

  setup(fx, 120.f, 640);
  fx.touchEvent(0, k_unit_touch_phase_began, 0, 640);
  if (!fx.debugRunning())
  {
    std::printf("not running after touch\n");
    return 4;
  }

  std::vector<float> bar120;
  render(fx, bar120, 96000U);
  const float peak120 = peakAbs(bar120);
  const float rms120 = rms(bar120);
  const uint32_t triggers120 = fx.debugTriggerCount();
  std::printf("120bpm peak=%.4f rms=%.4f triggers=%u\n", peak120, rms120, triggers120);
  if (peak120 < 0.08f || peak120 > 0.99f)
    return 5;
  if (rms120 < 0.01f)
    return 6;
  if (triggers120 < 15U || triggers120 > 18U)
    return 7;

  setup(fx, 240.f, 640);
  fx.touchEvent(0, k_unit_touch_phase_began, 0, 640);
  std::vector<float> bar240;
  render(fx, bar240, 48000U);
  const uint32_t triggers240 = fx.debugTriggerCount();
  const float peak240 = peakAbs(bar240);
  std::printf("240bpm peak=%.4f triggers=%u\n", peak240, triggers240);
  if (peak240 < 0.08f)
    return 8;
  if (triggers240 < 15U || triggers240 > 18U)
    return 9;

  setup(fx, 120.f, 640);
  fx.setParameter(AmenTime::RPT, 1);
  fx.touchEvent(0, k_unit_touch_phase_began, 0, 640);
  std::vector<float> locked;
  render(fx, locked, 48000U);
  if (fx.debugTriggerCount() < 7U)
  {
    std::printf("lock triggers=%u\n", fx.debugTriggerCount());
    return 10;
  }
  if (peakAbs(locked) < 0.05f)
    return 11;

  setup(fx, 120.f, 640);
  fx.touchEvent(0, k_unit_touch_phase_began, 0, 640);
  std::vector<float> before_release;
  render(fx, before_release, 256U);
  fx.touchEvent(0, k_unit_touch_phase_ended, 0, 640);
  if (fx.debugRunning())
  {
    std::printf("still running after gate release\n");
    return 12;
  }
  std::vector<float> after_release;
  render(fx, after_release, 4096U);
  const float tail_peak = peakAbs(std::vector<float>(after_release.end() - 256, after_release.end()));
  std::printf("gate tail peak=%.4f\n", tail_peak);
  if (tail_peak > 0.02f)
    return 13;

  setup(fx, 120.f, 100);
  fx.setParameter(AmenTime::STRT, 1023);
  fx.touchEvent(0, k_unit_touch_phase_began, 0, 100);
  std::vector<float> wrapped;
  render(fx, wrapped, 96000U);
  const float wrap_peak = peakAbs(wrapped);
  std::printf("wrap strt peak=%.4f triggers=%u\n", wrap_peak, fx.debugTriggerCount());
  if (wrap_peak < 0.08f)
    return 14;
  if (fx.debugTriggerCount() < 3U)
    return 15;

  std::printf("ok\n");
  return 0;
}
