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
  const uint32_t step = beat / 4U; // 16 steps/bar
  const uint32_t total = beat * 8U;

  std::vector<float> input(total, 0.f);
  fillImpulseBar(input, step);

  std::vector<float> ram(192000U * 2U, 0.f);
  std::vector<float> out(total, 0.f);

  EucRoll fx;
  fx.init(ram.data());
  fx.setTempo(bpm);
  // dens_norm ~ 0 → 1 hit only (step 0). Sparse pattern leaves most steps dry.
  fx.setParameter(EucRoll::DENS, 0);
  fx.setParameter(EucRoll::ROLL, 1023);
  fx.setParameter(EucRoll::PAN, 0);
  fx.setParameter(EucRoll::MIX, 1000);
  fx.setParameter(EucRoll::STEPS, 2);
  fx.setParameter(EucRoll::GLUE, 0);

  processRange(fx, input, out, 0U, beat * 4U);
  fx.touchEvent(0, k_unit_touch_phase_began, 512U, 512U);
  processRange(fx, input, out, beat * 4U, beat * 4U);
  fx.touchEvent(0, k_unit_touch_phase_ended, 512U, 512U);

  const uint32_t bar2 = beat * 4U;
  // With 1 euclid hit on 16 steps, only step 0 rolls. Step 1 should be dry
  // (pass-through of the live impulse), not a continued micro-roll.
  const float hit_head = windowRms(out, bar2, 48U);
  const float miss_mid = windowRms(out, bar2 + step + step / 4U, 64U);
  const float miss_err = std::fabs(out[bar2 + step] - input[bar2 + step]);

  // High dens + roll: verify micro-stutter still works on hits.
  EucRoll dense;
  dense.init(ram.data());
  dense.setTempo(bpm);
  dense.setParameter(EucRoll::DENS, 1023);
  dense.setParameter(EucRoll::ROLL, 1023);
  dense.setParameter(EucRoll::PAN, 0);
  dense.setParameter(EucRoll::MIX, 1000);
  dense.setParameter(EucRoll::STEPS, 2);
  dense.setParameter(EucRoll::GLUE, 0);
  std::vector<float> dense_out(total, 0.f);
  processRange(dense, input, dense_out, 0U, beat * 4U);
  dense.touchEvent(0, k_unit_touch_phase_began, 512U, 512U);
  processRange(dense, input, dense_out, beat * 4U, beat * 4U);

  const uint32_t probe = bar2 + step;
  const uint32_t micro = step / 8U;
  float head_rms = 0.f;
  float mid_rms = 0.f;
  for (uint32_t microIndex = 0; microIndex < 8U; ++microIndex)
  {
    head_rms += windowRms(dense_out, probe + microIndex * micro, 48U);
    mid_rms += windowRms(dense_out, probe + microIndex * micro + micro / 2U, 48U);
  }
  head_rms /= 8.f;
  mid_rms /= 8.f;

  // Random pan: left/right should diverge with Depth up.
  EucRoll pan_fx;
  pan_fx.init(ram.data());
  pan_fx.setTempo(bpm);
  pan_fx.setParameter(EucRoll::DENS, 1023);
  pan_fx.setParameter(EucRoll::ROLL, 1023);
  pan_fx.setParameter(EucRoll::PAN, 1000);
  pan_fx.setParameter(EucRoll::MIX, 1000);
  pan_fx.setParameter(EucRoll::STEPS, 2);
  pan_fx.setParameter(EucRoll::GLUE, 0);
  std::vector<float> pan_l(total, 0.f);
  std::vector<float> pan_r(total, 0.f);
  processRange(pan_fx, input, pan_l, 0U, beat * 4U); // warm-up writes L only via helper
  // Re-run stereo capture for pan check.
  {
    pan_fx.reset();
    pan_fx.init(ram.data());
    pan_fx.setTempo(bpm);
    pan_fx.setParameter(EucRoll::DENS, 1023);
    pan_fx.setParameter(EucRoll::ROLL, 700);
    pan_fx.setParameter(EucRoll::PAN, 1000);
    pan_fx.setParameter(EucRoll::MIX, 1000);
    pan_fx.setParameter(EucRoll::STEPS, 2);
    pan_fx.setParameter(EucRoll::GLUE, 0);
    const uint32_t frames = total;
    std::vector<float> interleaved_in(frames * 2U, 0.f);
    std::vector<float> interleaved_out(frames * 2U, 0.f);
    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      interleaved_in[sampleIndex * 2U] = input[sampleIndex];
      interleaved_in[sampleIndex * 2U + 1U] = input[sampleIndex];
    }
    const uint32_t warm = beat * 4U;
    for (uint32_t frameOffset = 0; frameOffset < warm; frameOffset += 64U)
      pan_fx.process(interleaved_in.data() + frameOffset * 2U, interleaved_out.data() + frameOffset * 2U, 64U);
    pan_fx.touchEvent(0, k_unit_touch_phase_began, 512U, 512U);
    for (uint32_t frameOffset = warm; frameOffset < frames; frameOffset += 64U)
      pan_fx.process(interleaved_in.data() + frameOffset * 2U, interleaved_out.data() + frameOffset * 2U, 64U);

    double sum_abs_diff = 0.0;
    uint32_t counted = 0U;
    for (uint32_t sampleIndex = warm; sampleIndex < frames; ++sampleIndex)
    {
      const float left = interleaved_out[sampleIndex * 2U];
      const float right = interleaved_out[sampleIndex * 2U + 1U];
      if (std::fabs(left) + std::fabs(right) < 0.05f)
        continue;
      sum_abs_diff += std::fabs(static_cast<double>(left - right));
      ++counted;
    }
    if (counted < 64U)
    {
      std::printf("FAIL: too few active samples for pan check (%u)\n", counted);
      return 1;
    }
    const float mean_lr_diff = static_cast<float>(sum_abs_diff / static_cast<double>(counted));

    std::printf("hit_head=%.6f miss_mid=%.6f miss_err=%.6f head_rms=%.6f mid_rms=%.6f lr_diff=%.6f active=%u\n",
                hit_head, miss_mid, miss_err, head_rms, mid_rms, mean_lr_diff, counted);

    if (miss_err > 1e-4f)
    {
      std::printf("FAIL: non-hit step should stay dry (pass-through)\n");
      return 1;
    }
    if (miss_mid > 0.05f)
    {
      std::printf("FAIL: non-hit mid-step should not keep rolling\n");
      return 1;
    }
    if (!(head_rms > mid_rms * 2.f))
    {
      std::printf("FAIL: expected click-then-gap inside hit-step rolls\n");
      return 1;
    }
    if (mean_lr_diff < 0.05f)
    {
      std::printf("FAIL: expected L/R divergence with random pan depth\n");
      return 1;
    }

    std::printf("OK\n");
    return 0;
  }
}
