#include "kaocid.h"
#include "macros.h"
#include "runtime.h"

#include <cmath>
#include <cstdio>
#include <vector>

static float windowRms(const std::vector<float> &mono, uint32_t start_sample, uint32_t count)
{
  double sum_squares = 0.0;
  uint32_t used = 0U;
  for (uint32_t sampleIndex = 0; sampleIndex < count; ++sampleIndex)
  {
    const uint32_t index = start_sample + sampleIndex;
    if (index >= mono.size())
      break;
    const double sample = static_cast<double>(mono[index]);
    sum_squares += sample * sample;
    ++used;
  }
  if (used == 0U)
    return 0.f;
  return static_cast<float>(std::sqrt(sum_squares / static_cast<double>(used)));
}

static void renderHeld(Kaocid &synth, uint32_t block_count, std::vector<float> &mono)
{
  constexpr uint32_t kBlockSize = 64U;
  std::vector<float> block(kBlockSize * 2U, 0.f);
  mono.assign(kBlockSize * block_count, 0.f);

  for (uint32_t blockIndex = 0; blockIndex < block_count; ++blockIndex)
  {
    synth.process(block.data(), block.data(), kBlockSize);
    for (uint32_t sampleIndex = 0; sampleIndex < kBlockSize; ++sampleIndex)
    {
      const float sample = block[sampleIndex * 2U];
      if (!std::isfinite(sample))
      {
        std::printf("non-finite sample at block %u\n", blockIndex);
        mono[blockIndex * kBlockSize + sampleIndex] = 0.f;
        continue;
      }
      mono[blockIndex * kBlockSize + sampleIndex] = sample;
    }
  }
}

int main()
{
  Kaocid synth;
  synth.init(nullptr);
  synth.setParameter(Kaocid::MIX, 1000);
  synth.setParameter(Kaocid::CUT, 700);
  synth.setParameter(Kaocid::RES, 600);
  synth.setParameter(Kaocid::ENV, 614);
  synth.setParameter(Kaocid::DEC, 410);
  synth.setParameter(Kaocid::ACC, 563);
  synth.setTempo(120.f);
  synth.touchEvent(0, k_unit_touch_phase_began, 512, 512);

  // Firmware may re-send began/stationary while the pad is held.
  for (uint32_t refreshIndex = 0; refreshIndex < 8U; ++refreshIndex)
    synth.touchEvent(0, k_unit_touch_phase_stationary, 512, 512);
  synth.touchEvent(0, k_unit_touch_phase_began, 520, 500);

  std::vector<float> mono;
  renderHeld(synth, 750U, mono);

  const float attack_rms = windowRms(mono, 0U, 480U);
  const float body_rms = windowRms(mono, 2400U, 4800U);
  const float later_rms = windowRms(mono, 24000U, 4800U);

  float peak = 0.f;
  for (uint32_t sampleIndex = 0; sampleIndex < mono.size(); ++sampleIndex)
  {
    const float magnitude = std::fabs(mono[sampleIndex]);
    if (magnitude > peak)
      peak = magnitude;
  }

  std::printf("peak=%.5f attack_rms=%.5f body_rms=%.5f later_rms=%.5f\n", peak, attack_rms, body_rms,
              later_rms);

  if (peak < 0.02f || peak > 1.2f)
    return 1;
  // Body after the 4 ms attack must stay audible; this is the hardware click bug.
  if (body_rms < 0.02f)
    return 2;
  if (later_rms < 0.008f)
    return 3;
  if (body_rms < attack_rms * 0.25f)
    return 4;
  return 0;
}
