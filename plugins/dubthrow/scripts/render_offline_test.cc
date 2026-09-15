#include "dubthrow.h"
#include "runtime.h"

#include <cmath>
#include <cstdio>
#include <vector>

static float windowPeak(const std::vector<float> &mono, uint32_t start_sample, uint32_t count)
{
  float peak = 0.f;
  for (uint32_t sampleIndex = 0; sampleIndex < count; ++sampleIndex)
  {
    const uint32_t index = start_sample + sampleIndex;
    if (index >= mono.size())
      break;
    const float sample = mono[index] < 0.f ? -mono[index] : mono[index];
    if (sample > peak)
      peak = sample;
  }
  return peak;
}

static void render(DubThrow &fx, const float *left, const float *right, uint32_t frames,
                   bool touching, std::vector<float> &mono_out)
{
  if (touching)
    fx.touchEvent(0, k_unit_touch_phase_began, 1023U, 512U);
  else
    fx.touchEvent(0, k_unit_touch_phase_ended, 0U, 0U);

  const uint32_t block_frames = 64U;
  std::vector<float> in_block(block_frames * 2U, 0.f);
  std::vector<float> out_block(block_frames * 2U, 0.f);
  uint32_t frameOffset = 0U;
  while (frameOffset < frames)
  {
    const uint32_t this_block =
        (frames - frameOffset) > block_frames ? block_frames : (frames - frameOffset);
    for (uint32_t sampleIndex = 0; sampleIndex < this_block; ++sampleIndex)
    {
      const uint32_t sourceIndex = frameOffset + sampleIndex;
      in_block[sampleIndex * 2U] = left[sourceIndex];
      in_block[sampleIndex * 2U + 1U] = right[sourceIndex];
    }
    fx.process(in_block.data(), out_block.data(), this_block);
    for (uint32_t sampleIndex = 0; sampleIndex < this_block; ++sampleIndex)
      mono_out.push_back(out_block[sampleIndex * 2U]);
    frameOffset += this_block;
  }
}

int main()
{
  DubThrow fx;
  std::vector<float> ram(fx.getBufferSize(), 0.f);
  fx.init(ram.data());
  fx.setTempo(120.f);
  fx.setParameter(DubThrow::THROW, 1023);
  fx.setParameter(DubThrow::TONE, 563);
  fx.setParameter(DubThrow::DEPTH, 1000);
  fx.setParameter(DubThrow::TIME, DubThrow::TIME_8D);
  fx.setParameter(DubThrow::FDBK, 700);
  fx.setParameter(DubThrow::SPRD, 358);
  fx.setParameter(DubThrow::MODE, DubThrow::MODE_PPONG);
  fx.setParameter(DubThrow::TOUCH, DubThrow::TOUCH_GATE);

  const uint32_t burst_frames = 4800U;   // 100 ms click/tone burst
  const uint32_t silence_frames = 96000U; // 2 s for dotted-eighth tails @ 120
  std::vector<float> burst_left(burst_frames, 0.f);
  std::vector<float> burst_right(burst_frames, 0.f);
  for (uint32_t sampleIndex = 0; sampleIndex < burst_frames; ++sampleIndex)
  {
    const float env = sampleIndex < 48U ? sampleIndex / 48.f : 1.f;
    const float sample = env * 0.5f * sinf(6.28318530718f * 220.f * sampleIndex / 48000.f);
    burst_left[sampleIndex] = sample;
    burst_right[sampleIndex] = sample * 0.9f;
  }
  std::vector<float> silent_left(silence_frames, 0.f);
  std::vector<float> silent_right(silence_frames, 0.f);

  std::vector<float> mono;
  render(fx, burst_left.data(), burst_right.data(), burst_frames, true, mono);
  render(fx, silent_left.data(), silent_right.data(), silence_frames, false, mono);

  // Dotted 1/8 @ 120 BPM = 0.75 beat = 375 ms = 18000 samples.
  const float peak_first = windowPeak(mono, 18000U, 2000U);
  const float peak_second = windowPeak(mono, 36000U, 2000U);
  const float peak_late = windowPeak(mono, 80000U, 2000U);
  const float dry_region = windowPeak(mono, 100U, 1000U);

  std::printf("dry_burst_peak=%.4f first_echo=%.4f second_echo=%.4f late=%.4f\n", dry_region,
              peak_first, peak_second, peak_late);

  if (dry_region < 0.1f)
  {
    std::printf("FAIL: dry path too quiet\n");
    return 1;
  }
  if (peak_first < 0.02f)
  {
    std::printf("FAIL: first echo missing (send/gate or delay broken)\n");
    return 1;
  }
  if (peak_second < 0.005f)
  {
    std::printf("FAIL: feedback tail too short\n");
    return 1;
  }
  if (peak_late > peak_first)
  {
    std::printf("FAIL: limiter/feedback runaway\n");
    return 1;
  }
  std::printf("OK\n");
  return 0;
}
