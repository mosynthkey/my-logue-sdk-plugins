#include "ukgarage.h"
#include "macros.h"
#include "runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static void render(UKGarage &kit, std::vector<float> &mono, uint32_t frames)
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

static void setup(UKGarage &kit, int32_t ghost, int32_t fill)
{
  kit.init(nullptr);
  kit.setParameter(UKGarage::MIX, 1000);
  kit.setParameter(UKGarage::GHOST, ghost);
  kit.setParameter(UKGarage::FILL, fill);
  kit.setParameter(UKGarage::SWING, 560);
  kit.setParameter(UKGarage::TONE, 460);
  kit.setParameter(UKGarage::DEC, 460);
  kit.setTempo(134.f);
}

int main()
{
  UKGarage kit;
  setup(kit, 700, 200);
  kit.debugResetCounters();

  // Spine: kick on 0/10, snare on 4/12 should fire mains.
  kit.debugTriggerStep(0U);
  kit.debugTriggerStep(4U);
  kit.debugTriggerStep(10U);
  kit.debugTriggerStep(12U);
  if (kit.debugMainTriggers() < 4U)
  {
    std::printf("spine mains=%u\n", kit.debugMainTriggers());
    return 1;
  }

  // High ghost: many soft seats should produce ghost triggers across a bar.
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

  // Low ghost: fewer ghosts than high ghost.
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

  // Envelope length: single kick should still have energy past 20 ms (not click-only).
  setup(kit, 0, 0);
  kit.debugTriggerStep(0U);
  std::vector<float> mono;
  render(kit, mono, 4800U); // 100 ms
  const float early = energyAfter(mono, 0U, 480U);     // 0–10 ms
  const float late = energyAfter(mono, 960U, 960U);    // 20–40 ms
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

  // Running clock for ~1 bar at 134 BPM should make audible output.
  setup(kit, 500, 400);
  kit.touchEvent(0, k_unit_touch_phase_began, 400U, 400U);
  mono.clear();
  const uint32_t bar_samples = static_cast<uint32_t>(48000.f * 60.f / 134.f * 4.f);
  render(kit, mono, bar_samples);
  const float bar_peak = peakAbs(mono);
  if (bar_peak < 0.08f)
  {
    std::printf("bar peak too quiet: %f\n", bar_peak);
    return 6;
  }

  std::printf("ok ghosts_high=%u ghosts_low=%u kick_peak=%f late_ratio=%f bar_peak=%f\n",
              high_ghosts, low_ghosts, peak, late / (early + 1e-9f), bar_peak);
  return 0;
}
