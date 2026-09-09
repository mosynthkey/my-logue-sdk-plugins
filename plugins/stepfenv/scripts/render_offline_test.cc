#include "stepfenv.h"
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

static void fillSine(std::vector<float> &interleaved, float hz, float sample_rate)
{
  const float increment = hz / sample_rate;
  float phase = 0.f;
  const uint32_t frames = static_cast<uint32_t>(interleaved.size() / 2U);
  for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
  {
    const float sample = std::sin(6.283185307179586f * phase);
    interleaved[sampleIndex * 2U] = sample;
    interleaved[sampleIndex * 2U + 1U] = sample;
    phase += increment;
    if (phase >= 1.f)
      phase -= 1.f;
  }
}

static void renderMono(StepFenv &fx, const std::vector<float> &interleaved_in, std::vector<float> &mono_out)
{
  const uint32_t frames = static_cast<uint32_t>(mono_out.size());
  std::vector<float> interleaved_out(frames * 2U, 0.f);
  const uint32_t block = 64U;
  for (uint32_t frameOffset = 0; frameOffset < frames; frameOffset += block)
  {
    const uint32_t n = (frameOffset + block <= frames) ? block : (frames - frameOffset);
    fx.process(interleaved_in.data() + frameOffset * 2U, interleaved_out.data() + frameOffset * 2U, n);
  }
  for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    mono_out[sampleIndex] = interleaved_out[sampleIndex * 2U];
}

int main()
{
  const float bpm = 120.f;
  const float sr = 48000.f;
  const uint32_t beat = static_cast<uint32_t>(sr * 60.f / bpm);
  // PERIOD_2STEP = 2 sixteenths = eighth note at 4/4.
  const uint32_t step8 = beat / 2U;
  const uint32_t total = beat * 4U;

  std::vector<float> input(total * 2U, 0.f);
  fillSine(input, 2000.f, sr);

  StepFenv fx;
  fx.init(nullptr);
  fx.setTempo(bpm);
  fx.setParameter(StepFenv::CUT, 154);
  fx.setParameter(StepFenv::ENV, 1023);
  fx.setParameter(StepFenv::MIX, 1000);
  fx.setParameter(StepFenv::DEC, 360);
  fx.setParameter(StepFenv::RES, 200);
  fx.setParameter(StepFenv::STEPS, StepFenv::PERIOD_2STEP);
  fx.setParameter(StepFenv::SHAPE, StepFenv::SHAPE_SAW);
  fx.reset();
  fx.touchEvent(0, k_unit_touch_phase_began, 512, 512);

  std::vector<float> out(total, 0.f);
  renderMono(fx, input, out);

  const float peak = windowRms(out, 0U, 48U);
  const float mid_decay = windowRms(out, 2400U, 256U);
  const float late_decay = windowRms(out, 9000U, 256U);
  const float retrig = windowRms(out, step8, 48U);

  bool nan_or_huge = false;
  for (uint32_t sampleIndex = 0; sampleIndex < out.size(); ++sampleIndex)
  {
    const float sample = out[sampleIndex];
    if (!std::isfinite(sample) || std::fabs(sample) > 8.f)
    {
      nan_or_huge = true;
      break;
    }
  }

  StepFenv closed;
  closed.init(nullptr);
  closed.setTempo(bpm);
  closed.setParameter(StepFenv::CUT, 154);
  closed.setParameter(StepFenv::ENV, 0);
  closed.setParameter(StepFenv::MIX, 1000);
  closed.setParameter(StepFenv::DEC, 360);
  closed.setParameter(StepFenv::RES, 200);
  closed.setParameter(StepFenv::STEPS, StepFenv::PERIOD_2STEP);
  closed.setParameter(StepFenv::SHAPE, StepFenv::SHAPE_SAW);
  closed.reset();
  closed.touchEvent(0, k_unit_touch_phase_began, 512, 512);
  std::vector<float> closed_out(total, 0.f);
  renderMono(closed, input, closed_out);
  const float closed_peak = windowRms(closed_out, 0U, 48U);
  const float closed_late = windowRms(closed_out, 9000U, 256U);

  StepFenv dry;
  dry.init(nullptr);
  dry.setTempo(bpm);
  dry.setParameter(StepFenv::CUT, 154);
  dry.setParameter(StepFenv::ENV, 1023);
  dry.setParameter(StepFenv::MIX, 1000);
  dry.setParameter(StepFenv::DEC, 360);
  dry.setParameter(StepFenv::RES, 200);
  dry.setParameter(StepFenv::STEPS, StepFenv::PERIOD_2STEP);
  dry.reset();
  std::vector<float> dry_out(total, 0.f);
  renderMono(dry, input, dry_out);
  const float dry_peak = windowRms(dry_out, 0U, 48U);
  const float dry_late = windowRms(dry_out, 9000U, 256U);

  std::printf("peak=%.6f mid=%.6f late=%.6f retrig=%.6f closed_peak=%.6f closed_late=%.6f dry_peak=%.6f\n",
              peak, mid_decay, late_decay, retrig, closed_peak, closed_late, dry_peak);

  if (nan_or_huge)
  {
    std::printf("FAIL: non-finite or exploding sample\n");
    return 1;
  }
  if (!(peak > 0.15f))
  {
    std::printf("FAIL: expected an open attack on each step\n");
    return 1;
  }
  if (!(peak > late_decay * 3.f))
  {
    std::printf("FAIL: envelope should still be open well past 20 ms (not a click)\n");
    return 1;
  }
  if (!(mid_decay > late_decay * 1.4f))
  {
    std::printf("FAIL: expected a decaying body, not an instant close\n");
    return 1;
  }
  if (!(retrig > late_decay * 2.5f))
  {
    std::printf("FAIL: next step should retrigger the filter env\n");
    return 1;
  }
  if (!(closed_late < peak * 0.35f) || !(std::fabs(closed_peak - closed_late) < 0.08f))
  {
    std::printf("FAIL: ENV=0 should stay near the resting cutoff\n");
    return 1;
  }
  if (!(std::fabs(dry_peak - dry_late) < 0.05f) || !(dry_peak > 0.5f))
  {
    std::printf("FAIL: without touch the unit should stay dry (bypass)\n");
    return 1;
  }

  std::printf("OK\n");
  return 0;
}
