#include "macros.h"
#include "ride909.h"
#include "runtime.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

int main(int argc, char **argv)
{
  const char *output_path = (argc > 1) ? argv[1] : "/tmp/ride909_preview.wav";

  Ride909 ride;
  ride.init(nullptr);
  ride.setParameter(Ride909::MIX, 1000);
  ride.setParameter(Ride909::PUMP, 512);
  ride.setParameter(Ride909::PITCH, 512);
  ride.setTempo(128.f);
  ride.touchEvent(0, k_unit_touch_phase_began, 512, 256);

  constexpr uint32_t kSampleRate = 48000U;
  constexpr uint32_t kBlockSize = 128U;
  constexpr uint32_t kBlockCount = 600U;
  std::vector<float> mono(kBlockSize * kBlockCount, 0.f);
  std::vector<float> block(kBlockSize * 2U, 0.f);

  for (uint32_t blockIndex = 0; blockIndex < kBlockCount; ++blockIndex)
  {
    ride.process(block.data(), block.data(), kBlockSize);
    for (uint32_t sampleIndex = 0; sampleIndex < kBlockSize; ++sampleIndex)
      mono[blockIndex * kBlockSize + sampleIndex] = block[sampleIndex * 2U];
  }

  FILE *file = std::fopen(output_path, "wb");
  if (!file)
    return 1;

  const uint32_t data_bytes = static_cast<uint32_t>(mono.size() * sizeof(float));
  const uint32_t riff_size = 36U + data_bytes;

  std::fwrite("RIFF", 1, 4, file);
  std::fwrite(&riff_size, 4, 1, file);
  std::fwrite("WAVE", 1, 4, file);
  std::fwrite("fmt ", 1, 4, file);
  const uint32_t fmt_size = 16U;
  const uint16_t audio_format = 3U;
  const uint16_t channel_count = 1U;
  const uint32_t byte_rate = kSampleRate * sizeof(float);
  const uint16_t block_align = sizeof(float);
  const uint16_t bits_per_sample = 32U;
  std::fwrite(&fmt_size, 4, 1, file);
  std::fwrite(&audio_format, 2, 1, file);
  std::fwrite(&channel_count, 2, 1, file);
  std::fwrite(&kSampleRate, 4, 1, file);
  std::fwrite(&byte_rate, 4, 1, file);
  std::fwrite(&block_align, 2, 1, file);
  std::fwrite(&bits_per_sample, 2, 1, file);
  std::fwrite("data", 1, 4, file);
  std::fwrite(&data_bytes, 4, 1, file);
  std::fwrite(mono.data(), sizeof(float), mono.size(), file);
  std::fclose(file);

  float peak = 0.f;
  for (float sample : mono)
    peak = std::fmax(peak, std::fabs(sample));

  std::printf("wrote %s peak=%.6f duration=%.2fs\n", output_path, peak,
              static_cast<float>(mono.size()) / static_cast<float>(kSampleRate));
  return peak > 0.01f ? 0 : 1;
}
