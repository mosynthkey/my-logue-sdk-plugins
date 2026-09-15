#include "macros.h"
#include "ride909.h"
#include "runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static float peakOf(const std::vector<float> &buffer)
{
  float peak = 0.f;
  for (float sample : buffer)
    peak = std::fmax(peak, std::fabs(sample));
  return peak;
}

static int testRelativeStep3AfterTouch()
{
  Ride909 ride;
  ride.init(nullptr);
  ride.setParameter(Ride909::MIX, 1000);
  ride.setParameter(Ride909::PUMP, 0);
  ride.setParameter(Ride909::PITCH, 512);
  ride.setTempo(120.f);
  ride.touchEvent(0, k_unit_touch_phase_began, 512, 0);

  constexpr uint32_t kBlockSize = 128U;
  std::vector<float> buffer(kBlockSize * 2U, 0.f);
  float early_peak = 0.f;
  for (uint32_t counter = 1U; counter < 3U; ++counter)
  {
    ride.tempo4ppqnTick(counter);
    std::fill(buffer.begin(), buffer.end(), 0.f);
    ride.process(buffer.data(), buffer.data(), kBlockSize);
    early_peak = std::fmax(early_peak, peakOf(buffer));
  }

  ride.tempo4ppqnTick(3U);
  float peak = 0.f;
  double sum_squares = 0.0;
  uint64_t sample_count = 0U;

  for (uint32_t blockIndex = 0; blockIndex < 400U; ++blockIndex)
  {
    std::fill(buffer.begin(), buffer.end(), 0.f);
    ride.process(buffer.data(), buffer.data(), kBlockSize);
    for (uint32_t sampleIndex = 0; sampleIndex < kBlockSize * 2U; ++sampleIndex)
    {
      const float sample = buffer[sampleIndex];
      peak = std::fmax(peak, std::fabs(sample));
      sum_squares += static_cast<double>(sample) * static_cast<double>(sample);
      ++sample_count;
    }
  }

  const double rms = std::sqrt(sum_squares / static_cast<double>(sample_count));
  std::printf("relative_step3 early_peak=%.6f ride_peak=%.6f rms=%.6f\n", early_peak, peak, rms);
  if (early_peak > 0.000001f)
  {
    std::printf("ride triggered before relative step 3\n");
    return 1;
  }
  return peak > 0.01f ? 0 : 2;
}

static int testLateTapSnapsToPreviousClock()
{
  Ride909 ride;
  ride.init(nullptr);
  ride.setParameter(Ride909::MIX, 1000);
  ride.setParameter(Ride909::PUMP, 0);
  ride.setParameter(Ride909::PITCH, 512);
  ride.setTempo(120.f);

  constexpr uint32_t kBlockSize = 128U;
  std::vector<float> buffer(kBlockSize * 2U, 0.f);

  // Establish the host grid without the pad held.
  ride.tempo4ppqnTick(1U);

  // Sit just after that tick so the previous clock is nearest.
  const uint32_t near_prev = 64U;
  ride.process(buffer.data(), buffer.data(), near_prev);

  ride.touchEvent(0, k_unit_touch_phase_began, 512, 0);
  if (ride.debugNextStep() != 2U)
  {
    std::printf("late tap should arm next tick as relative step 2 (got %u)\n", ride.debugNextStep());
    return 3;
  }

  float early_peak = 0.f;
  ride.tempo4ppqnTick(2U); // relative step 2 — silent
  std::fill(buffer.begin(), buffer.end(), 0.f);
    ride.process(buffer.data(), buffer.data(), kBlockSize);
  early_peak = peakOf(buffer);

  ride.tempo4ppqnTick(3U); // relative step 3 — ride
  float ride_peak = 0.f;
  for (uint32_t blockIndex = 0; blockIndex < 400U; ++blockIndex)
  {
    std::fill(buffer.begin(), buffer.end(), 0.f);
    ride.process(buffer.data(), buffer.data(), kBlockSize);
    ride_peak = std::fmax(ride_peak, peakOf(buffer));
  }

  std::printf("late_tap early_peak=%.6f ride_peak=%.6f next_step=%u\n", early_peak, ride_peak,
              ride.debugNextStep());
  if (early_peak > 0.000001f)
    return 4;
  return ride_peak > 0.01f ? 0 : 5;
}

static int testEarlyTapWaitsForNextClock()
{
  Ride909 ride;
  ride.init(nullptr);
  ride.setParameter(Ride909::MIX, 1000);
  ride.setParameter(Ride909::PUMP, 0);
  ride.setParameter(Ride909::PITCH, 512);
  ride.setTempo(120.f);

  constexpr uint32_t kSampleRate = 48000U;
  const uint32_t samples_per_tick = static_cast<uint32_t>(kSampleRate * 60.f / (120.f * 4.f));
  std::vector<float> buffer(samples_per_tick * 2U, 0.f);

  ride.tempo4ppqnTick(1U);
  // Sit just before the next tick so the upcoming clock is nearest.
  const uint32_t near_next = samples_per_tick - 64U;
  ride.process(buffer.data(), buffer.data(), near_next);

  ride.touchEvent(0, k_unit_touch_phase_began, 512, 0);
  if (ride.debugNextStep() != 1U)
  {
    std::printf("early tap should arm next tick as relative step 1 (got %u)\n", ride.debugNextStep());
    return 6;
  }

  float early_peak = 0.f;
  ride.tempo4ppqnTick(2U); // relative step 1 — pump only
  std::fill(buffer.begin(), buffer.end(), 0.f);
    ride.process(buffer.data(), buffer.data(), 128U);
  early_peak = peakOf(buffer);
  ride.tempo4ppqnTick(3U); // relative step 2
  std::fill(buffer.begin(), buffer.end(), 0.f);
    ride.process(buffer.data(), buffer.data(), 128U);
  early_peak = std::fmax(early_peak, peakOf(buffer));

  ride.tempo4ppqnTick(4U); // relative step 3 — ride
  float ride_peak = 0.f;
  for (uint32_t blockIndex = 0; blockIndex < 400U; ++blockIndex)
  {
    std::fill(buffer.begin(), buffer.end(), 0.f);
    ride.process(buffer.data(), buffer.data(), 128U);
    ride_peak = std::fmax(ride_peak, peakOf(buffer));
  }

  std::printf("early_tap early_peak=%.6f ride_peak=%.6f\n", early_peak, ride_peak);
  if (early_peak > 0.000001f)
    return 7;
  return ride_peak > 0.01f ? 0 : 8;
}

int main()
{
  const int relative = testRelativeStep3AfterTouch();
  if (relative != 0)
    return relative;
  const int late = testLateTapSnapsToPreviousClock();
  if (late != 0)
    return late;
  return testEarlyTapWaitsForNextClock();
}
