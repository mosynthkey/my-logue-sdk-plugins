#include "stepsaw.h"
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
    const float abs_sample = std::fabs(mono[index]);
    if (abs_sample > peak)
      peak = abs_sample;
  }
  return peak;
}

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

static void processRange(StepSaw &fx, std::vector<float> &output, uint32_t frames)
{
  const uint32_t block = 64U;
  std::vector<float> silent(block * 2U, 0.f);
  for (uint32_t frameOffset = 0; frameOffset < frames; frameOffset += block)
  {
    const uint32_t n = (frameOffset + block <= frames) ? block : (frames - frameOffset);
    std::vector<float> out_block(n * 2U, 0.f);
    fx.process(silent.data(), out_block.data(), n);
    for (uint32_t sampleIndex = 0; sampleIndex < n; ++sampleIndex)
      output[frameOffset + sampleIndex] = out_block[sampleIndex * 2U];
  }
}

int main()
{
  const float bpm = 120.f;
  const uint32_t sr = 48000U;
  const uint32_t beat = static_cast<uint32_t>(sr * 60.f / bpm);
  const uint32_t step = beat / 4U; // 16 steps/bar
  const uint32_t total = beat * 2U;

  StepSaw fx;
  fx.init(nullptr);
  fx.setTempo(bpm);
  fx.setParameter(StepSaw::PITCH, 563);
  fx.setParameter(StepSaw::DEC, 200);
  fx.setParameter(StepSaw::MIX, 1000);
  fx.setParameter(StepSaw::STEPS, 0); // 16
  fx.setParameter(StepSaw::LVL, 1023);

  std::vector<float> out(total, 0.f);
  processRange(fx, out, total);

  int failures = 0;
  for (uint32_t stepIndex = 0; stepIndex < 8U; ++stepIndex)
  {
    const uint32_t head = stepIndex * step;
    const float head_peak = windowPeak(out, head, 64U);
    const float mid_rms = windowRms(out, head + step / 2U, 64U);
    if (head_peak < 0.05f)
    {
      std::printf("FAIL: step %u head peak too quiet (%.4f)\n", stepIndex, head_peak);
      ++failures;
    }
    if (mid_rms > head_peak * 0.35f)
    {
      std::printf("FAIL: step %u mid should be quieter than head (head=%.4f mid=%.4f)\n", stepIndex,
                  head_peak, mid_rms);
      ++failures;
    }
  }

  // Host 4ppqn path: after a tick arrives, markers follow the host counter.
  StepSaw host_fx;
  host_fx.init(nullptr);
  host_fx.setTempo(bpm);
  host_fx.setParameter(StepSaw::DEC, 100);
  host_fx.setParameter(StepSaw::MIX, 1000);
  host_fx.setParameter(StepSaw::LVL, 1023);
  host_fx.setParameter(StepSaw::STEPS, 0);
  host_fx.tempo4ppqnTick(1U);
  if (!host_fx.debugUsesHostClock())
  {
    std::printf("FAIL: expected host clock after tempo4ppqnTick\n");
    ++failures;
  }
  if (!host_fx.debugEnvActive() || host_fx.debugAge() > 0.f)
  {
    std::printf("FAIL: host tick should retrigger env at age 0\n");
    ++failures;
  }

  std::vector<float> host_out(step, 0.f);
  processRange(host_fx, host_out, step);
  const float host_head = windowPeak(host_out, 0U, 64U);
  const float host_tail = windowRms(host_out, step - 64U, 64U);
  if (host_head < 0.05f || host_tail > host_head * 0.35f)
  {
    std::printf("FAIL: host-clock marker should decay (head=%.4f tail=%.4f)\n", host_head, host_tail);
    ++failures;
  }

  if (failures != 0)
  {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("OK: StepSaw fires decaying saws on internal and host step heads\n");
  return 0;
}
