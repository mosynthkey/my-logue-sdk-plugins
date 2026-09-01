#include "autrance.h"
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

static void renderBlock(Autrance &synth, uint32_t frames, std::vector<float> &mono_accum)
{
  std::vector<float> block(frames * 2U, 0.f);
  synth.process(block.data(), block.data(), frames);
  for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    mono_accum.push_back(block[sampleIndex * 2U]);
}

int main()
{
  Autrance synth;
  synth.init(nullptr);
  synth.setParameter(Autrance::MIX, 1000);
  synth.setParameter(Autrance::CUT, 593);
  synth.setParameter(Autrance::RES, 430);
  synth.setParameter(Autrance::DEC, 461);
  synth.setParameter(Autrance::ENV, 665);
  synth.setParameter(Autrance::ROOT, 36);
  synth.setTempo(120.f);
  synth.touchEvent(0, k_unit_touch_phase_began, 512U, 512U);

  std::vector<float> mono;
  const uint32_t total_frames = 48000U;
  const uint32_t block_frames = 64U;
  for (uint32_t frameOffset = 0; frameOffset < total_frames; frameOffset += block_frames)
    renderBlock(synth, block_frames, mono);

  const float attack_rms = windowRms(mono, 200U, 400U);
  const float body_rms = windowRms(mono, 2000U, 4000U);
  const float tail_rms = windowRms(mono, 8000U, 4000U);

  std::printf("attack_rms=%.6f body_rms=%.6f tail_rms=%.6f\n", attack_rms, body_rms, tail_rms);

  if (body_rms < 0.002f)
  {
    std::printf("body too quiet; expected audible pluck decay\n");
    return 10;
  }

  if (body_rms < attack_rms * 0.08f)
  {
    std::printf("body too weak relative to attack (click-only symptom)\n");
    return 11;
  }

  return 0;
}
