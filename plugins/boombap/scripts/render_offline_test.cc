#include "boombap.h"
#include "macros.h"
#include "runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static void render(BoomBap &kit, std::vector<float> &mono, uint32_t frames)
{
  std::vector<float> block(frames * 2U, 0.f);
  kit.process(block.data(), block.data(), frames);
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

static float energyAfter(const std::vector<float> &mono, uint32_t start, uint32_t length)
{
  double energy = 0.0;
  const uint32_t end = std::min(static_cast<uint32_t>(mono.size()), start + length);
  for (uint32_t sampleIndex = start; sampleIndex < end; ++sampleIndex)
  {
    const double sample = static_cast<double>(mono[sampleIndex]);
    energy += sample * sample;
  }
  return static_cast<float>(energy);
}

static void setup(BoomBap &kit, int32_t ghost, int32_t hats)
{
  kit.init(nullptr);
  kit.setParameter(BoomBap::MIX, 1000);
  kit.setParameter(BoomBap::GHOST, ghost);
  kit.setParameter(BoomBap::HATS, hats);
  kit.setParameter(BoomBap::SWING, 630);
  kit.setParameter(BoomBap::TONE, 410);
  kit.setParameter(BoomBap::DEC, 510);
  kit.setTempo(90.f);
}

int main()
{
  BoomBap kit;
  setup(kit, 700, 200);
  kit.debugResetCounters();

  // Spine: kick 0/7/10, snare 4/12.
  kit.debugTriggerStep(0U);
  kit.debugTriggerStep(4U);
  kit.debugTriggerStep(7U);
  kit.debugTriggerStep(10U);
  kit.debugTriggerStep(12U);
  if (kit.debugMainTriggers() < 5U)
  {
    std::printf("spine mains=%u\n", kit.debugMainTriggers());
    return 1;
  }

  kit.debugResetCounters();
  for (uint32_t passIndex = 0; passIndex < 8U; ++passIndex)
  {
    for (uint32_t stepIndex = 0; stepIndex < 16U; ++stepIndex)
      kit.debugTriggerStep(stepIndex);
  }
  if (kit.debugGhostTriggers() < 8U)
  {
    std::printf("ghost triggers too low: %u\n", kit.debugGhostTriggers());
    return 2;
  }

  const uint32_t high_ghosts = kit.debugGhostTriggers();
  setup(kit, 80, 200);
  kit.debugResetCounters();
  for (uint32_t passIndex = 0; passIndex < 8U; ++passIndex)
  {
    for (uint32_t stepIndex = 0; stepIndex < 16U; ++stepIndex)
      kit.debugTriggerStep(stepIndex);
  }
  const uint32_t low_ghosts = kit.debugGhostTriggers();
  if (low_ghosts >= high_ghosts)
  {
    std::printf("low ghost=%u not below high=%u\n", low_ghosts, high_ghosts);
    return 3;
  }

  setup(kit, 0, 0);
  kit.debugTriggerStep(0U);
  std::vector<float> mono;
  render(kit, mono, 4800U);
  const float early = energyAfter(mono, 0U, 480U);
  const float late = energyAfter(mono, 960U, 960U);
  const float peak = peakAbs(mono);
  if (peak < 0.05f)
  {
    std::printf("kick peak too quiet: %f\n", peak);
    return 4;
  }
  if (late < early * 0.02f)
  {
    std::printf("kick dies like a click: early=%f late=%f peak=%f\n", early, late, peak);
    return 5;
  }

  setup(kit, 500, 400);
  kit.debugResetCounters();
  kit.touchEvent(0, k_unit_touch_phase_began, 400U, 400U);
  if (kit.debugMainTriggers() != 0U)
  {
    std::printf("touch fired immediately: mains=%u\n", kit.debugMainTriggers());
    return 6;
  }

  mono.clear();
  render(kit, mono, 2000U);
  if (kit.debugMainTriggers() != 0U || peakAbs(mono) > 0.02f)
  {
    std::printf("pre-grid leak: mains=%u peak=%f\n", kit.debugMainTriggers(), peakAbs(mono));
    return 7;
  }

  mono.clear();
  const uint32_t bar_samples = static_cast<uint32_t>(48000.f * 60.f / 90.f * 4.f);
  render(kit, mono, bar_samples);
  const float bar_peak = peakAbs(mono);
  if (bar_peak < 0.08f || kit.debugMainTriggers() < 4U)
  {
    std::printf("bar peak too quiet: peak=%f mains=%u\n", bar_peak, kit.debugMainTriggers());
    return 8;
  }

  setup(kit, 500, 400);
  kit.debugResetCounters();
  kit.tempo4ppqnTick(1U);
  if (kit.debugMainTriggers() != 0U)
  {
    std::printf("host tick while up fired: %u\n", kit.debugMainTriggers());
    return 9;
  }
  kit.touchEvent(0, k_unit_touch_phase_began, 400U, 400U);
  kit.tempo4ppqnTick(5U); // step 4 = snare
  if (kit.debugMainTriggers() < 1U)
  {
    std::printf("host mid-bar tick missed snare: mains=%u\n", kit.debugMainTriggers());
    return 10;
  }

  std::printf("ok ghosts_high=%u ghosts_low=%u kick_peak=%f late_ratio=%f bar_peak=%f\n",
              high_ghosts, low_ghosts, peak, late / (early + 1e-9f), bar_peak);
  return 0;
}
