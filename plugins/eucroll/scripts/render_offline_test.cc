#include "eucroll.h"
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

static void fillImpulseBar(std::vector<float> &input, uint32_t step)
{
  for (uint32_t sampleIndex = 0; sampleIndex < input.size(); ++sampleIndex)
    input[sampleIndex] = 0.f;
  for (uint32_t sampleIndex = 0; sampleIndex < input.size(); sampleIndex += step)
  {
    for (uint32_t clickIndex = 0; clickIndex < 48U && sampleIndex + clickIndex < input.size(); ++clickIndex)
      input[sampleIndex + clickIndex] = 1.f - static_cast<float>(clickIndex) / 48.f;
  }
}

static void processRange(EucRoll &fx, std::vector<float> &input, std::vector<float> &output,
                         uint32_t start, uint32_t frames)
{
  const uint32_t block = 64U;
  for (uint32_t frameOffset = 0; frameOffset < frames; frameOffset += block)
  {
    const uint32_t n = (frameOffset + block <= frames) ? block : (frames - frameOffset);
    const uint32_t abs = start + frameOffset;
    std::vector<float> in_block(n * 2U, 0.f);
    std::vector<float> out_block(n * 2U, 0.f);
    for (uint32_t sampleIndex = 0; sampleIndex < n; ++sampleIndex)
    {
      in_block[sampleIndex * 2U] = input[abs + sampleIndex];
      in_block[sampleIndex * 2U + 1U] = input[abs + sampleIndex];
    }
    fx.process(in_block.data(), out_block.data(), n);
    for (uint32_t sampleIndex = 0; sampleIndex < n; ++sampleIndex)
      output[abs + sampleIndex] = out_block[sampleIndex * 2U];
  }
}

int main()
{
  const float bpm = 120.f;
  const uint32_t sr = 48000U;
  const uint32_t beat = static_cast<uint32_t>(sr * 60.f / bpm);
  const uint32_t step = beat / 4U;
  const uint32_t total = beat * 8U; // two bars

  std::vector<float> input(total, 0.f);
  fillImpulseBar(input, step);

  std::vector<float> ram(192000U * 2U, 0.f);
  std::vector<float> dry_out(total, 0.f);
  std::vector<float> roll_out(total, 0.f);

  EucRoll dry_fx;
  dry_fx.init(ram.data());
  dry_fx.setTempo(bpm);
  dry_fx.setParameter(EucRoll::DENS, 400);
  dry_fx.setParameter(EucRoll::ROLL, 0);
  dry_fx.setParameter(EucRoll::MIX, 1000);
  dry_fx.setParameter(EucRoll::STEPS, 2);
  processRange(dry_fx, input, dry_out, 0U, total);
  const float dry_err = std::fabs(dry_out[step] - input[step]);

  EucRoll roll_fx;
  roll_fx.init(ram.data());
  roll_fx.setTempo(bpm);
  roll_fx.setParameter(EucRoll::DENS, 1023);
  roll_fx.setParameter(EucRoll::ROLL, 1023);
  roll_fx.setParameter(EucRoll::MIX, 1000);
  roll_fx.setParameter(EucRoll::STEPS, 2);
  roll_fx.setParameter(EucRoll::GLUE, 0);

  // Pre-roll one bar dry so the ring buffer holds a full step.
  processRange(roll_fx, input, roll_out, 0U, beat * 4U);
  roll_fx.touchEvent(0, k_unit_touch_phase_began, 512U, 512U);
  processRange(roll_fx, input, roll_out, beat * 4U, beat * 4U);
  roll_fx.touchEvent(0, k_unit_touch_phase_ended, 512U, 512U);

  const uint32_t probe_start = beat * 4U + step;
  const uint32_t micro = step / 8U;
  float head_rms = 0.f;
  float mid_rms = 0.f;
  uint32_t loud_heads = 0U;
  for (uint32_t microIndex = 0; microIndex < 8U; ++microIndex)
  {
    const uint32_t head = probe_start + microIndex * micro;
    const uint32_t mid = head + micro / 2U;
    const float head_value = windowRms(roll_out, head, 48U);
    const float mid_value = windowRms(roll_out, mid, 48U);
    head_rms += head_value;
    mid_rms += mid_value;
    if (head_value > 0.2f)
      ++loud_heads;
  }
  head_rms /= 8.f;
  mid_rms /= 8.f;

  const float dry_rms = windowRms(dry_out, step, 64U);
  const float roll_peak = windowRms(roll_out, probe_start, 48U);

  std::printf("dry_err=%.6f dry_rms=%.6f roll_peak=%.6f head_rms=%.6f mid_rms=%.6f loud_heads=%u\n",
              dry_err, dry_rms, roll_peak, head_rms, mid_rms, loud_heads);

  if (dry_err > 1e-4f)
  {
    std::printf("FAIL: dry path should pass input when not touching\n");
    return 1;
  }
  if (roll_peak < 0.2f)
  {
    std::printf("FAIL: rolled path too quiet after touch\n");
    return 1;
  }
  if (loud_heads < 4U)
  {
    std::printf("FAIL: expected repeated micro-roll hits inside the step\n");
    return 1;
  }
  if (!(head_rms > mid_rms * 2.f))
  {
    std::printf("FAIL: expected click-then-gap inside each roll subdivision\n");
    return 1;
  }

  std::printf("OK\n");
  return 0;
}
