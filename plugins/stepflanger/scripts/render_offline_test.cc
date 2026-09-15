#include "stepflanger.h"
#include "runtime.h"

#include <cmath>
#include <cstdio>
#include <vector>

static float windowRms(const std::vector<float> &mono, uint32_t start_sample, uint32_t count)
{
  double sum = 0.0;
  uint32_t used = 0U;
  for (uint32_t sampleIndex = 0; sampleIndex < count; ++sampleIndex)
  {
    const uint32_t index = start_sample + sampleIndex;
    if (index >= mono.size())
      break;
    const float sample = mono[index];
    sum += static_cast<double>(sample) * static_cast<double>(sample);
    ++used;
  }
  if (used == 0U)
    return 0.f;
  return static_cast<float>(std::sqrt(sum / static_cast<double>(used)));
}

int main()
{
  StepFlanger fx;
  std::vector<float> ram(fx.getBufferSize(), 0.f);
  fx.init(ram.data());
  fx.setTempo(120.f);
  fx.setParameter(StepFlanger::RATE, 400);
  fx.setParameter(StepFlanger::DEPTH, 900); // upper half: +feedback, deep LFO
  fx.setParameter(StepFlanger::MIX, 1000);
  fx.setParameter(StepFlanger::STEPS, StepFlanger::PERIOD_1STEP);
  fx.setParameter(StepFlanger::TIME, 400);
  fx.setParameter(StepFlanger::SLEW, 100);

  fx.touchEvent(0, k_unit_touch_phase_began, 512U, 200U);

  const uint32_t total_frames = 48000U; // 1 s
  std::vector<float> mono_out;
  mono_out.reserve(total_frames);

  const uint32_t block_frames = 64U;
  std::vector<float> in_block(block_frames * 2U, 0.f);
  std::vector<float> out_block(block_frames * 2U, 0.f);

  uint32_t frameOffset = 0U;
  while (frameOffset < total_frames)
  {
    const uint32_t this_block =
        (total_frames - frameOffset) > block_frames ? block_frames : (total_frames - frameOffset);
    for (uint32_t sampleIndex = 0; sampleIndex < this_block; ++sampleIndex)
    {
      const uint32_t sourceIndex = frameOffset + sampleIndex;
      const float sample = 0.4f * sinf(6.28318530718f * 440.f * sourceIndex / 48000.f);
      in_block[sampleIndex * 2U] = sample;
      in_block[sampleIndex * 2U + 1U] = sample;
    }
    fx.process(in_block.data(), out_block.data(), this_block);
    for (uint32_t sampleIndex = 0; sampleIndex < this_block; ++sampleIndex)
      mono_out.push_back(out_block[sampleIndex * 2U]);
    frameOffset += this_block;
  }

  const float early = windowRms(mono_out, 0U, 4800U);
  const float mid = windowRms(mono_out, 20000U, 4800U);
  const float late = windowRms(mono_out, 40000U, 4800U);

  std::printf("rms early=%.4f mid=%.4f late=%.4f\n", early, mid, late);

  if (early < 0.01f || mid < 0.01f || late < 0.01f)
  {
    std::printf("FAIL: output too quiet\n");
    return 1;
  }
  if (early > 1.5f || mid > 1.5f || late > 1.5f)
  {
    std::printf("FAIL: output runaway\n");
    return 1;
  }

  // Negative feedback path should also produce audible wet.
  StepFlanger fx_neg;
  std::vector<float> ram_neg(fx_neg.getBufferSize(), 0.f);
  fx_neg.init(ram_neg.data());
  fx_neg.setTempo(120.f);
  fx_neg.setParameter(StepFlanger::RATE, 400);
  fx_neg.setParameter(StepFlanger::DEPTH, 100); // lower half: −feedback
  fx_neg.setParameter(StepFlanger::MIX, 1000);
  fx_neg.setParameter(StepFlanger::STEPS, StepFlanger::PERIOD_HALF);
  fx_neg.setParameter(StepFlanger::TIME, 400);
  fx_neg.setParameter(StepFlanger::SLEW, 100);
  fx_neg.touchEvent(0, k_unit_touch_phase_began, 512U, 900U);

  std::vector<float> mono_neg;
  mono_neg.reserve(total_frames);
  frameOffset = 0U;
  while (frameOffset < total_frames)
  {
    const uint32_t this_block =
        (total_frames - frameOffset) > block_frames ? block_frames : (total_frames - frameOffset);
    for (uint32_t sampleIndex = 0; sampleIndex < this_block; ++sampleIndex)
    {
      const uint32_t sourceIndex = frameOffset + sampleIndex;
      const float sample = 0.4f * sinf(6.28318530718f * 440.f * sourceIndex / 48000.f);
      in_block[sampleIndex * 2U] = sample;
      in_block[sampleIndex * 2U + 1U] = sample;
    }
    fx_neg.process(in_block.data(), out_block.data(), this_block);
    for (uint32_t sampleIndex = 0; sampleIndex < this_block; ++sampleIndex)
      mono_neg.push_back(out_block[sampleIndex * 2U]);
    frameOffset += this_block;
  }

  const float neg_mid = windowRms(mono_neg, 20000U, 4800U);
  std::printf("rms neg_mid=%.4f\n", neg_mid);
  if (neg_mid < 0.01f)
  {
    std::printf("FAIL: negative-feedback path too quiet\n");
    return 1;
  }

  std::printf("OK\n");
  return 0;
}
