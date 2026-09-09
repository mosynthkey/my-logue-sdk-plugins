#include "trap808.h"
#include "macros.h"
#include "runtime.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static void render(Trap808 &drum, std::vector<float> &mono, uint32_t frames)
{
  std::vector<float> block(frames * 2U, 0.f);
  drum.process(block.data(), block.data(), frames);
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

static float rmsAfter(const std::vector<float> &mono, uint32_t start, uint32_t length)
{
  if (start >= mono.size())
    return 0.f;
  const uint32_t end = static_cast<uint32_t>(std::min<size_t>(mono.size(), start + length));
  double sum = 0.0;
  uint32_t count = 0U;
  for (uint32_t sampleIndex = start; sampleIndex < end; ++sampleIndex)
  {
    const double sample = static_cast<double>(mono[sampleIndex]);
    sum += sample * sample;
    ++count;
  }
  if (count == 0U)
    return 0.f;
  return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

static void setup(Trap808 &drum, int32_t hats, int32_t groove, int32_t root)
{
  drum.init(nullptr);
  drum.setParameter(Trap808::MIX, 1000);
  drum.setParameter(Trap808::HATS, hats);
  drum.setParameter(Trap808::GROOVE, groove);
  drum.setParameter(Trap808::ROOT, root);
  drum.setParameter(Trap808::DECAY, 700);
  drum.setParameter(Trap808::DRIVE, 500);
  drum.setTempo(140.f);
  drum.debugResetCounters();
}

int main()
{
  Trap808 drum;

  setup(drum, 100, 300, 33);
  drum.touchEvent(0, k_unit_touch_phase_began, 200, 200);
  std::vector<float> sparse;
  render(drum, sparse, 48000U);
  const uint32_t sparse_hats = drum.debugHatTriggers();
  const uint32_t sparse_kicks = drum.debugKickTriggers();
  const float sparse_peak = peakAbs(sparse);
  std::printf("sparse hats=%u kicks=%u peak=%.4f\n", sparse_hats, sparse_kicks, sparse_peak);
  if (sparse_peak < 0.05f || sparse_peak > 1.2f)
    return 1;
  if (sparse_kicks < 1U)
    return 2;

  setup(drum, 980, 300, 33);
  drum.touchEvent(0, k_unit_touch_phase_began, 900, 200);
  std::vector<float> dense;
  render(drum, dense, 48000U);
  const uint32_t dense_hats = drum.debugHatTriggers();
  const float dense_peak = peakAbs(dense);
  std::printf("dense hats=%u peak=%.4f\n", dense_hats, dense_peak);
  if (dense_hats <= sparse_hats + 8U)
    return 3;
  if (dense_peak < 0.05f || dense_peak > 1.25f)
    return 4;

  setup(drum, 200, 200, 36);
  drum.touchEvent(0, k_unit_touch_phase_began, 200, 200);
  std::vector<float> body;
  render(drum, body, 48000U);
  // First 4ppqn tick at 140 BPM is ~5143 samples — inspect after that.
  const float early = rmsAfter(body, 5200U, 960U);
  const float mid = rmsAfter(body, 10000U, 4800U);
  std::printf("808 early_rms=%.5f mid_rms=%.5f\n", early, mid);
  if (early < 0.01f)
    return 5;
  if (mid < 0.01f)
    return 6;
  if (mid * 8.f < early)
    return 7;

  setup(drum, 500, 400, 33);
  drum.touchEvent(0, k_unit_touch_phase_began, 200, 200);
  if (drum.debugHatTriggers() != 0U || drum.debugKickTriggers() != 0U)
  {
    std::printf("tap fired early hats=%u kicks=%u\n", drum.debugHatTriggers(), drum.debugKickTriggers());
    return 8;
  }
  std::vector<float> before_tick;
  render(drum, before_tick, 2000U);
  if (drum.debugHatTriggers() != 0U || drum.debugKickTriggers() != 0U)
  {
    std::printf("pre-tick hits hats=%u kicks=%u\n", drum.debugHatTriggers(), drum.debugKickTriggers());
    return 9;
  }
  std::vector<float> after_tick;
  render(drum, after_tick, 8000U);
  if (drum.debugKickTriggers() < 1U && drum.debugHatTriggers() < 1U)
  {
    std::printf("no beat-locked hits after clock\n");
    return 10;
  }

  setup(drum, 500, 400, 33);
  drum.touchEvent(0, k_unit_touch_phase_began, 900, 900);
  std::vector<float> fill;
  render(drum, fill, 48000U);
  const uint32_t fill_hats = drum.debugHatTriggers();
  std::printf("fill hats=%u\n", fill_hats);
  if (fill_hats < 8U)
    return 11;

  std::printf("ok\n");
  return 0;
}
