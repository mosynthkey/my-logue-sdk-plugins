#include "runtime.h"
#include "transitionlooper.h"

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

static float windowLowRatio(const std::vector<float> &mono, uint32_t start_sample, uint32_t count)
{
  double low_squares = 0.0;
  double all_squares = 0.0;
  float lp = 0.f;
  const float coeff = 0.02f;
  uint32_t used = 0U;
  for (uint32_t sampleIndex = 0; sampleIndex < count; ++sampleIndex)
  {
    const uint32_t index = start_sample + sampleIndex;
    if (index >= mono.size())
      break;
    const float sample = mono[index];
    lp += coeff * (sample - lp);
    low_squares += static_cast<double>(lp) * static_cast<double>(lp);
    all_squares += static_cast<double>(sample) * static_cast<double>(sample);
    ++used;
  }
  if (used == 0U || all_squares < 1.0e-12)
    return 0.f;
  return static_cast<float>(low_squares / all_squares);
}

static void renderWithInput(TransitionLooper &fx, const float *left, const float *right, uint32_t frames,
                            std::vector<float> &mono_out)
{
  const uint32_t block_frames = 64U;
  std::vector<float> block(block_frames * 2U, 0.f);
  uint32_t frameOffset = 0U;
  while (frameOffset < frames)
  {
    const uint32_t this_block = (frames - frameOffset) > block_frames ? block_frames : (frames - frameOffset);
    for (uint32_t sampleIndex = 0; sampleIndex < this_block; ++sampleIndex)
    {
      const uint32_t sourceIndex = frameOffset + sampleIndex;
      block[sampleIndex * 2U] = left[sourceIndex];
      block[sampleIndex * 2U + 1U] = right[sourceIndex];
    }
    fx.process(block.data(), block.data(), this_block);
    for (uint32_t sampleIndex = 0; sampleIndex < this_block; ++sampleIndex)
      mono_out.push_back(block[sampleIndex * 2U]);
    frameOffset += this_block;
  }
}

static void fillTone(std::vector<float> &left, std::vector<float> &right, float hz, float amp)
{
  const float phase_inc = 6.28318530718f * hz / 48000.f;
  float phase = 0.f;
  for (uint32_t sampleIndex = 0; sampleIndex < left.size(); ++sampleIndex)
  {
    const float sample = sinf(phase) * amp;
    left[sampleIndex] = sample;
    right[sampleIndex] = sample * 0.85f;
    phase += phase_inc;
    if (phase > 6.28318530718f)
      phase -= 6.28318530718f;
  }
}

static void fillTwoTone(std::vector<float> &left, std::vector<float> &right, float low_hz, float high_hz, float amp)
{
  const float low_inc = 6.28318530718f * low_hz / 48000.f;
  const float high_inc = 6.28318530718f * high_hz / 48000.f;
  float low_phase = 0.f;
  float high_phase = 0.f;
  for (uint32_t sampleIndex = 0; sampleIndex < left.size(); ++sampleIndex)
  {
    const float sample = (sinf(low_phase) + sinf(high_phase)) * (amp * 0.5f);
    left[sampleIndex] = sample;
    right[sampleIndex] = sample * 0.85f;
    low_phase += low_inc;
    high_phase += high_inc;
    if (low_phase > 6.28318530718f)
      low_phase -= 6.28318530718f;
    if (high_phase > 6.28318530718f)
      high_phase -= 6.28318530718f;
  }
}

int main()
{
  TransitionLooper fx;
  std::vector<float> ram(fx.getBufferSize(), 0.f);
  fx.init(ram.data());
  fx.setTempo(120.f);
  fx.setParameter(TransitionLooper::TIME, 80);
  fx.setParameter(TransitionLooper::TONE, 700);
  fx.setParameter(TransitionLooper::MIX, 1000);
  fx.setParameter(TransitionLooper::TYPE, TransitionLooper::TYPE_VOL);
  fx.setParameter(TransitionLooper::GLUE, 400);
  fx.setParameter(TransitionLooper::SYNC, TransitionLooper::SYNC_4);

  const uint32_t bar_frames = 96000U;
  const uint32_t extra_frames = 8000U;
  std::vector<float> left(bar_frames + extra_frames, 0.f);
  std::vector<float> right(bar_frames + extra_frames, 0.f);
  fillTone(left, right, 220.f, 0.4f);

  std::vector<float> bypass;
  renderWithInput(fx, left.data(), right.data(), 2048U, bypass);
  float bypass_err = 0.f;
  for (uint32_t sampleIndex = 256U; sampleIndex < 2048U; ++sampleIndex)
  {
    const float delta = bypass[sampleIndex] - left[sampleIndex];
    bypass_err += delta >= 0.f ? delta : -delta;
  }
  bypass_err /= 1792.f;
  std::printf("bypass_err=%.6f captured=%u\n", bypass_err, fx.capturedSamples());
  if (bypass_err > 0.02f)
  {
    std::printf("idle path should stay close to the dry input\n");
    return 10;
  }

  std::vector<float> primed;
  renderWithInput(fx, left.data(), right.data(), bar_frames, primed);
  std::printf("captured_after_bar=%u frozen=%d wet=%.3f loop=%u\n", fx.capturedSamples(), fx.isFrozen() ? 1 : 0,
              fx.wetAmount(), fx.loopLength());
  if (fx.capturedSamples() < 80000U)
  {
    std::printf("expected a nearly full 16-step capture before tap\n");
    return 11;
  }

  fx.touchEvent(0, k_unit_touch_phase_began, 512U, 512U);
  std::vector<float> silent_left(24000U, 0.f);
  std::vector<float> silent_right(24000U, 0.f);
  std::vector<float> held;
  renderWithInput(fx, silent_left.data(), silent_right.data(), 24000U, held);

  const float fade_rms = windowRms(held, 200U, 400U);
  const float loop_rms = windowRms(held, 8000U, 8000U);
  std::printf("fade_rms=%.6f loop_rms=%.6f frozen=%d wet=%.3f\n", fade_rms, loop_rms, fx.isFrozen() ? 1 : 0,
              fx.wetAmount());
  if (loop_rms < 0.05f)
  {
    std::printf("held pad should play the stored loop after the input goes silent\n");
    return 12;
  }
  if (!fx.isFrozen() || fx.wetAmount() < 0.9f)
  {
    std::printf("held pad should stay on the frozen loop\n");
    return 13;
  }

  fx.touchEvent(0, k_unit_touch_phase_ended, 512U, 512U);
  std::vector<float> released;
  renderWithInput(fx, silent_left.data(), silent_right.data(), 24000U, released);
  const float release_tail = windowRms(released, 20000U, 3000U);
  std::printf("release_tail=%.6f frozen=%d\n", release_tail, fx.isFrozen() ? 1 : 0);
  if (release_tail > 0.01f)
  {
    std::printf("released pad should return to silence when the input is silent\n");
    return 14;
  }

  TransitionLooper hpf_fx;
  std::vector<float> hpf_ram(hpf_fx.getBufferSize(), 0.f);
  hpf_fx.init(hpf_ram.data());
  hpf_fx.setTempo(120.f);
  hpf_fx.setParameter(TransitionLooper::TIME, 470);
  hpf_fx.setParameter(TransitionLooper::TONE, 900);
  hpf_fx.setParameter(TransitionLooper::MIX, 1000);
  hpf_fx.setParameter(TransitionLooper::TYPE, TransitionLooper::TYPE_HPF);
  std::vector<float> two_left(bar_frames, 0.f);
  std::vector<float> two_right(bar_frames, 0.f);
  fillTwoTone(two_left, two_right, 70.f, 2500.f, 0.5f);
  std::vector<float> hpf_prime;
  renderWithInput(hpf_fx, two_left.data(), two_right.data(), bar_frames, hpf_prime);
  hpf_fx.touchEvent(0, k_unit_touch_phase_began, 512U, 512U);
  std::vector<float> hpf_in;
  renderWithInput(hpf_fx, silent_left.data(), silent_right.data(), 24000U, hpf_in);
  const float hpf_early_low = windowLowRatio(hpf_in, 2400U, 2400U);
  const float hpf_late_low = windowLowRatio(hpf_in, 19000U, 4000U);
  std::printf("hpf_early_low=%.6f hpf_late_low=%.6f\n", hpf_early_low, hpf_late_low);
  if (hpf_late_low < hpf_early_low * 1.25f + 0.03f)
  {
    std::printf("HPF fade-in should start thin and restore lows as the loop arrives\n");
    return 16;
  }

  return 0;
}
