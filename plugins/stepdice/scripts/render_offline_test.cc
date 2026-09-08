#include "stepdice.h"
#include "runtime.h"

#include <cmath>
#include <cstdio>
#include <vector>

static void fillTone(std::vector<float> &left, std::vector<float> &right, float hz, float amp)
{
  const float phase_inc = 6.28318530718f * hz / 48000.f;
  float phase = 0.f;
  for (uint32_t sampleIndex = 0; sampleIndex < left.size(); ++sampleIndex)
  {
    const float sample = std::sin(phase) * amp;
    left[sampleIndex] = sample;
    right[sampleIndex] = sample * 0.85f;
    phase += phase_inc;
    if (phase > 6.28318530718f)
      phase -= 6.28318530718f;
  }
}

static void renderMono(StepDice &fx, const std::vector<float> &left, const std::vector<float> &right,
                       std::vector<float> &mono_out)
{
  const uint32_t block_frames = 64U;
  std::vector<float> in_block(block_frames * 2U, 0.f);
  std::vector<float> out_block(block_frames * 2U, 0.f);
  uint32_t frameOffset = 0U;
  const uint32_t frames = static_cast<uint32_t>(left.size());
  mono_out.clear();
  mono_out.reserve(frames);
  while (frameOffset < frames)
  {
    const uint32_t this_block = (frames - frameOffset) > block_frames ? block_frames : (frames - frameOffset);
    for (uint32_t sampleIndex = 0; sampleIndex < this_block; ++sampleIndex)
    {
      const uint32_t sourceIndex = frameOffset + sampleIndex;
      in_block[sampleIndex * 2U] = left[sourceIndex];
      in_block[sampleIndex * 2U + 1U] = right[sourceIndex];
    }
    fx.process(in_block.data(), in_block.data(), out_block.data(), this_block);
    for (uint32_t sampleIndex = 0; sampleIndex < this_block; ++sampleIndex)
      mono_out.push_back(out_block[sampleIndex * 2U]);
    frameOffset += this_block;
  }
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

static float windowHighEnergy(const std::vector<float> &mono, uint32_t start_sample, uint32_t count)
{
  double sum = 0.0;
  uint32_t used = 0U;
  float prev = 0.f;
  for (uint32_t sampleIndex = 0; sampleIndex < count; ++sampleIndex)
  {
    const uint32_t index = start_sample + sampleIndex;
    if (index >= mono.size())
      break;
    const float sample = mono[index];
    const float delta = sample - prev;
    sum += static_cast<double>(delta * delta);
    prev = sample;
    ++used;
  }
  if (used == 0U)
    return 0.f;
  return static_cast<float>(sum / static_cast<double>(used));
}

static void setup(StepDice &fx, int32_t steps, int32_t mix, int32_t hold)
{
  fx.setTempo(120.f);
  fx.setParameter(StepDice::AMT, 900);
  fx.setParameter(StepDice::DICE, 220);
  fx.setParameter(StepDice::MIX, mix);
  fx.setParameter(StepDice::STEPS, steps);
  fx.setParameter(StepDice::BANK, StepDice::BANK_ALL);
  fx.setParameter(StepDice::DENS, 1023);
  fx.setParameter(StepDice::HOLD, hold);
}

int main()
{
  StepDice fx;
  std::vector<float> ram(fx.getBufferSize(), 0.f);
  fx.init(ram.data());

  const uint32_t bar_samples = 48000U * 2U;
  const uint32_t warmup_bars = 1U;
  const uint32_t total = bar_samples * (warmup_bars + 2U);
  std::vector<float> left(total, 0.f);
  std::vector<float> right(total, 0.f);
  fillTone(left, right, 220.f, 0.35f);

  setup(fx, 0, 1000, StepDice::HOLD_RUN);
  std::vector<float> wet16;
  renderMono(fx, left, right, wet16);

  const uint32_t measure = warmup_bars * bar_samples;
  const uint32_t step_len = bar_samples / 16U;
  float min_rms = 1.e9f;
  float max_rms = 0.f;
  float min_hi = 1.e9f;
  float max_hi = 0.f;
  uint32_t distinct_fx = 0U;
  uint8_t seen[StepDice::NUM_FX] = {};
  for (uint32_t stepIndex = 0; stepIndex < 16U; ++stepIndex)
  {
    const float rms = windowRms(wet16, measure + stepIndex * step_len + 128U, step_len - 256U);
    const float hi = windowHighEnergy(wet16, measure + stepIndex * step_len + 128U, step_len - 256U);
    if (rms < min_rms)
      min_rms = rms;
    if (rms > max_rms)
      max_rms = rms;
    if (hi < min_hi)
      min_hi = hi;
    if (hi > max_hi)
      max_hi = hi;
    const uint8_t fx_id = fx.stepFx(stepIndex);
    if (fx_id < StepDice::NUM_FX && seen[fx_id] == 0)
    {
      seen[fx_id] = 1;
      ++distinct_fx;
    }
    std::printf("step %u fx=%u rms=%.4f hi=%.5f\n", stepIndex, fx_id, rms, hi);
  }

  if (distinct_fx < 4U)
  {
    std::fprintf(stderr, "FAIL: expected several different FX, got %u\n", distinct_fx);
    return 1;
  }
  if (max_rms < min_rms * 1.25f && max_hi < min_hi * 1.35f)
  {
    std::fprintf(stderr, "FAIL: 16-step render is too uniform (rms %.4f..%.4f hi %.5f..%.5f)\n",
                 min_rms, max_rms, min_hi, max_hi);
    return 1;
  }

  fx.reset();
  setup(fx, 4, 1000, StepDice::HOLD_RUN);
  std::vector<float> wet1;
  renderMono(fx, left, right, wet1);
  const float first = windowRms(wet1, measure + 256U, bar_samples / 4U);
  const float second = windowRms(wet1, measure + bar_samples / 2U + 256U, bar_samples / 4U);
  const float ratio = (first > 1.e-5f) ? (second / first) : 0.f;
  if (ratio < 0.65f || ratio > 1.55f)
  {
    std::fprintf(stderr, "FAIL: STEPS=1 should stay consistent across the bar (%.3f vs %.3f)\n", first,
                 second);
    return 1;
  }

  fx.reset();
  setup(fx, 0, 0, StepDice::HOLD_RUN);
  std::vector<float> dry;
  renderMono(fx, left, right, dry);
  const float dry_err = std::fabs(windowRms(dry, measure, 4096U) - windowRms(left, measure, 4096U));
  if (dry_err > 0.01f)
  {
    std::fprintf(stderr, "FAIL: MIX=0 should pass dry (err=%.4f)\n", dry_err);
    return 1;
  }

  fx.reset();
  setup(fx, 0, 1000, StepDice::HOLD_PAD);
  std::vector<float> held;
  renderMono(fx, left, right, held);
  const float held_err = std::fabs(windowRms(held, measure, 4096U) - windowRms(left, measure, 4096U));
  if (held_err > 0.03f)
  {
    std::fprintf(stderr, "FAIL: HOLD without touch should stay dry (err=%.4f)\n", held_err);
    return 1;
  }

  std::printf("OK distinct_fx=%u rms=%.4f..%.4f hi=%.5f..%.5f steps1_ratio=%.3f\n", distinct_fx, min_rms,
              max_rms, min_hi, max_hi, ratio);
  return 0;
}
