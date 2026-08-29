#include "macros.h"
#include "ride909.h"
#include "runtime.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
struct TailMetrics
{
  float peak = 0.f;
  float tail_rms = 0.f;
  float late_rms = 0.f;
  float decay_40db_sec = 0.f;
};

TailMetrics measureTail(Ride909 &ride, int32_t pitch_value, uint32_t sample_rate)
{
  ride.init(nullptr);
  ride.setParameter(Ride909::MIX, 1000);
  ride.setParameter(Ride909::PUMP, 0);
  ride.setParameter(Ride909::PITCH, pitch_value);
  ride.setTempo(120.f);
  ride.touchEvent(0, k_unit_touch_phase_began, 512, 0);

  constexpr uint32_t kBlockSize = 128U;
  constexpr uint32_t kBlockCount = 500U;
  std::vector<float> mono(kBlockSize * kBlockCount, 0.f);
  std::vector<float> block(kBlockSize * 2U, 0.f);

  for (uint32_t blockIndex = 0; blockIndex < kBlockCount; ++blockIndex)
  {
    ride.process(block.data(), block.data(), kBlockSize);
    for (uint32_t sampleIndex = 0; sampleIndex < kBlockSize; ++sampleIndex)
      mono[blockIndex * kBlockSize + sampleIndex] = block[sampleIndex * 2U];
  }

  TailMetrics metrics;
  for (float sample : mono)
    metrics.peak = std::fmax(metrics.peak, std::fabs(sample));

  const float threshold_40db = metrics.peak * 0.01f;
  uint32_t hold_below = 0U;
  const uint32_t hold_required = sample_rate / 100U;
  for (uint32_t sampleIndex = 0; sampleIndex < mono.size(); ++sampleIndex)
  {
    if (std::fabs(mono[sampleIndex]) < threshold_40db)
    {
      ++hold_below;
      if (hold_below >= hold_required)
      {
        metrics.decay_40db_sec =
            static_cast<float>(sampleIndex - hold_required) / static_cast<float>(sample_rate);
        break;
      }
    }
    else
    {
      hold_below = 0U;
    }
  }

  const uint32_t tail_start = static_cast<uint32_t>(0.55f * static_cast<float>(sample_rate));
  const uint32_t tail_end = static_cast<uint32_t>(0.85f * static_cast<float>(sample_rate));
  const uint32_t late_start = static_cast<uint32_t>(0.85f * static_cast<float>(sample_rate));
  const uint32_t late_end = static_cast<uint32_t>(0.95f * static_cast<float>(sample_rate));

  auto window_rms = [&](uint32_t start, uint32_t end) {
    double sum_squares = 0.0;
    uint32_t count = 0U;
    for (uint32_t sampleIndex = start; sampleIndex < end && sampleIndex < mono.size(); ++sampleIndex)
    {
      const double sample = mono[sampleIndex];
      sum_squares += sample * sample;
      ++count;
    }
    return count > 0U ? static_cast<float>(std::sqrt(sum_squares / static_cast<double>(count))) : 0.f;
  };

  metrics.tail_rms = window_rms(tail_start, tail_end);
  metrics.late_rms = window_rms(late_start, late_end);
  return metrics;
}
} // namespace

int main()
{
  constexpr uint32_t kSampleRate = 48000U;
  const int32_t pitch_values[] = {512, 768, 1023};
  const char *pitch_labels[] = {"0 st", "+6 st", "+12 st"};

  std::printf("Ride909 tail energy by pitch\n");
  for (uint32_t pitchIndex = 0; pitchIndex < 3U; ++pitchIndex)
  {
    Ride909 ride;
    const TailMetrics metrics = measureTail(ride, pitch_values[pitchIndex], kSampleRate);
    std::printf("  %s: decay_40db=%.3fs tail_rms=%.6f late_rms=%.6f peak=%.4f\n", pitch_labels[pitchIndex],
                metrics.decay_40db_sec, metrics.tail_rms, metrics.late_rms, metrics.peak);
  }

  return 0;
}
