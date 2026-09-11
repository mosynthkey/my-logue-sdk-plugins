#include "trance2.h"
#include "macros.h"
#include "runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static void render(Trance2 &kit, std::vector<float> &mono, uint32_t frames)
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

static void setup(Trance2 &kit, int32_t bass, int32_t drum)
{
  kit.init(nullptr);
  kit.setParameter(Trance2::MIX, 1000);
  kit.setParameter(Trance2::BASS, bass);
  kit.setParameter(Trance2::DRUM, drum);
  kit.setParameter(Trance2::SWING, 200);
  kit.setParameter(Trance2::TONE, 460);
  kit.setParameter(Trance2::DEC, 460);
  kit.setTempo(138.f);
}

int main()
{
  Trance2 kit;
  setup(kit, 700, 200);
  kit.debugResetCounters();

  kit.debugTriggerStep(0U);
  kit.debugTriggerStep(4U);
  kit.debugTriggerStep(8U);
  kit.debugTriggerStep(12U);
  if (kit.debugMainTriggers() < 4U)
  {
    std::printf("spine mains=%u\n", kit.debugMainTriggers());
    return 1;
  }

  // Mid-high bass should fire rolling notes across non-kick seats.
  setup(kit, 850, 200);
  kit.debugResetCounters();
  for (uint32_t passIndex = 0; passIndex < 8U; ++passIndex)
  {
    for (uint32_t stepIndex = 0; stepIndex < 16U; ++stepIndex)
      kit.debugTriggerStep(stepIndex);
  }
  const uint32_t high_bass = kit.debugBassTriggers();
  if (high_bass < 40U)
  {
    std::printf("high bass triggers too low: %u\n", high_bass);
    return 2;
  }

  setup(kit, 120, 200);
  kit.debugResetCounters();
  for (uint32_t passIndex = 0; passIndex < 8U; ++passIndex)
  {
    for (uint32_t stepIndex = 0; stepIndex < 16U; ++stepIndex)
      kit.debugTriggerStep(stepIndex);
  }
  const uint32_t low_bass = kit.debugBassTriggers();
  if (low_bass >= high_bass)
  {
    std::printf("low bass=%u not below high=%u\n", low_bass, high_bass);
    return 3;
  }

  // Low drum → fewer hat ghosts than high drum.
  setup(kit, 400, 800);
  kit.debugResetCounters();
  for (uint32_t passIndex = 0; passIndex < 8U; ++passIndex)
  {
    for (uint32_t stepIndex = 0; stepIndex < 16U; ++stepIndex)
      kit.debugTriggerStep(stepIndex);
  }
  const uint32_t high_hats = kit.debugGhostTriggers();

  setup(kit, 400, 80);
  kit.debugResetCounters();
  for (uint32_t passIndex = 0; passIndex < 8U; ++passIndex)
  {
    for (uint32_t stepIndex = 0; stepIndex < 16U; ++stepIndex)
      kit.debugTriggerStep(stepIndex);
  }
  const uint32_t low_hats = kit.debugGhostTriggers();
  if (low_hats >= high_hats)
  {
    std::printf("low hats=%u not below high=%u\n", low_hats, high_hats);
    return 4;
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
    return 5;
  }
  if (late < early * 0.02f)
  {
    std::printf("kick dies like a click: early=%f late=%f peak=%f\n", early, late, peak);
    return 6;
  }

  // Bass note body should last past a click (~20 ms).
  setup(kit, 900, 0);
  kit.debugResetCounters();
  kit.debugTriggerStep(2U); // offbeat eighth bass seat
  mono.clear();
  render(kit, mono, 4800U);
  const float bass_peak = peakAbs(mono);
  const float bass_late = energyAfter(mono, 480U, 960U);
  if (kit.debugBassTriggers() < 1U || bass_peak < 0.02f || bass_late < 1e-5f)
  {
    std::printf("bass too weak: trig=%u peak=%f late=%f\n", kit.debugBassTriggers(), bass_peak, bass_late);
    return 7;
  }

  setup(kit, 500, 400);
  kit.debugResetCounters();
  kit.touchEvent(0, k_unit_touch_phase_began, 400U, 400U);
  if (kit.debugMainTriggers() != 0U)
  {
    std::printf("touch fired immediately: mains=%u\n", kit.debugMainTriggers());
    return 8;
  }

  mono.clear();
  const uint32_t bar_samples = static_cast<uint32_t>(48000.f * 60.f / 138.f * 4.f);
  render(kit, mono, bar_samples);
  const float bar_peak = peakAbs(mono);
  if (bar_peak < 0.08f || kit.debugMainTriggers() < 4U)
  {
    std::printf("bar peak too quiet: peak=%f mains=%u\n", bar_peak, kit.debugMainTriggers());
    return 9;
  }

  std::printf("ok bass_high=%u bass_low=%u hats_high=%u hats_low=%u kick_peak=%f bass_peak=%f bar_peak=%f\n",
              high_bass, low_bass, high_hats, low_hats, peak, bass_peak, bar_peak);
  return 0;
}
