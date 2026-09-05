#include "hclap.h"
#include "macros.h"
#include "runtime.h"

#include <cstdint>
#include <cstdio>
#include <vector>

static void writeWav(const char *path, const std::vector<float> &mono, uint32_t sample_rate)
{
  FILE *file = std::fopen(path, "wb");
  if (!file)
    return;

  const uint32_t data_bytes = static_cast<uint32_t>(mono.size() * 2U);
  const uint32_t chunk_size = 36U + data_bytes;
  const uint16_t audio_format = 1U;
  const uint16_t channels = 1U;
  const uint16_t bits = 16U;
  const uint32_t byte_rate = sample_rate * channels * bits / 8U;
  const uint16_t block_align = channels * bits / 8U;

  std::fwrite("RIFF", 1, 4, file);
  std::fwrite(&chunk_size, 4, 1, file);
  std::fwrite("WAVEfmt ", 1, 8, file);
  const uint32_t fmt_size = 16U;
  std::fwrite(&fmt_size, 4, 1, file);
  std::fwrite(&audio_format, 2, 1, file);
  std::fwrite(&channels, 2, 1, file);
  std::fwrite(&sample_rate, 4, 1, file);
  std::fwrite(&byte_rate, 4, 1, file);
  std::fwrite(&block_align, 2, 1, file);
  std::fwrite(&bits, 2, 1, file);
  std::fwrite("data", 1, 4, file);
  std::fwrite(&data_bytes, 4, 1, file);

  for (float sample : mono)
  {
    float clipped = sample;
    if (clipped > 1.f)
      clipped = 1.f;
    if (clipped < -1.f)
      clipped = -1.f;
    const int16_t pcm = static_cast<int16_t>(clipped * 32767.f);
    std::fwrite(&pcm, 2, 1, file);
  }
  std::fclose(file);
}

static void renderSeconds(HClap &clap, std::vector<float> &mono, float seconds)
{
  const uint32_t frames = static_cast<uint32_t>(48000.f * seconds);
  std::vector<float> block(128U * 2U, 0.f);
  uint32_t remaining = frames;
  while (remaining > 0U)
  {
    const uint32_t now = remaining > 128U ? 128U : remaining;
    clap.process(block.data(), block.data(), now);
    for (uint32_t sampleIndex = 0; sampleIndex < now; ++sampleIndex)
      mono.push_back(block[sampleIndex * 2U]);
    remaining -= now;
  }
}

int main(int argc, char **argv)
{
  const char *out_path = argc > 1 ? argv[1] : "/tmp/hclap_preview.wav";
  HClap clap;
  clap.init(nullptr);
  clap.setParameter(HClap::MIX, 1000);
  clap.setParameter(HClap::DENS, 70);
  clap.setParameter(HClap::TYPE, 0);
  clap.setParameter(HClap::TONE, 512);
  clap.setParameter(HClap::DEC, 512);
  clap.setParameter(HClap::SNAP, 512);
  clap.setTempo(120.f);
  clap.touchEvent(0, k_unit_touch_phase_began, 70, 0);

  std::vector<float> mono;
  renderSeconds(clap, mono, 2.f);
  writeWav(out_path, mono, 48000U);
  std::printf("wrote %s samples=%zu\n", out_path, mono.size());
  return 0;
}
