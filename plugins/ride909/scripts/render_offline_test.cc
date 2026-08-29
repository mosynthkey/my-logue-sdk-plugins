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
  std::printf("peak=%.6f rms=%.6f\n", peak, rms);
  return peak > 0.01f ? 0 : 1;
}
