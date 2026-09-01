#include "macros.h"
#include "ride909.h"
#include "runtime.h"

#include <cmath>
#include <cstdio>
#include <vector>

int main()
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
    ride.process(buffer.data(), buffer.data(), kBlockSize);
    for (float sample : buffer)
      early_peak = std::fmax(early_peak, std::fabs(sample));
  }

  ride.tempo4ppqnTick(3U);
  float peak = 0.f;
  double sum_squares = 0.0;
  uint64_t sample_count = 0U;

  for (uint32_t blockIndex = 0; blockIndex < 400U; ++blockIndex)
  {
    ride.process(buffer.data(), buffer.data(), kBlockSize);
    for (uint32_t sampleIndex = 0; sampleIndex < kBlockSize * 2U; ++sampleIndex)
    {
      const float sample = buffer[sampleIndex];
      const float magnitude = std::fabs(sample);
      if (magnitude > peak)
        peak = magnitude;
      sum_squares += static_cast<double>(sample) * static_cast<double>(sample);
      ++sample_count;
    }
  }

  const double rms = std::sqrt(sum_squares / static_cast<double>(sample_count));
  std::printf("early_peak=%.6f ride_step_peak=%.6f rms=%.6f\n", early_peak, peak, rms);
  if (early_peak > 0.000001f)
  {
    std::printf("ride triggered before scheduled step 3\n");
    return 1;
  }
  return peak > 0.01f ? 0 : 2;
}
