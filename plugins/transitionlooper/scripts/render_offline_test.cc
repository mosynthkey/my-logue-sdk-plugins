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

static void renderWithSplitInput(TransitionLooper &fx, const float *dry_left, const float *dry_right,
                                 const float *raw_left, const float *raw_right, uint32_t frames,
                                 std::vector<float> &mono_out)
{
  const uint32_t block_frames = 64U;
  std::vector<float> dry_block(block_frames * 2U, 0.f);
  std::vector<float> raw_block(block_frames * 2U, 0.f);
  std::vector<float> out_block(block_frames * 2U, 0.f);
  uint32_t frameOffset = 0U;
  while (frameOffset < frames)
  {
    const uint32_t this_block = (frames - frameOffset) > block_frames ? block_frames : (frames - frameOffset);
    for (uint32_t sampleIndex = 0; sampleIndex < this_block; ++sampleIndex)
    {
      const uint32_t sourceIndex = frameOffset + sampleIndex;
      dry_block[sampleIndex * 2U] = dry_left[sourceIndex];
      dry_block[sampleIndex * 2U + 1U] = dry_right[sourceIndex];
      raw_block[sampleIndex * 2U] = raw_left[sourceIndex];
      raw_block[sampleIndex * 2U + 1U] = raw_right[sourceIndex];
    }
    fx.process(dry_block.data(), raw_block.data(), out_block.data(), this_block);
    for (uint32_t sampleIndex = 0; sampleIndex < this_block; ++sampleIndex)
      mono_out.push_back(out_block[sampleIndex * 2U]);
    frameOffset += this_block;
  }
}

static void renderWithInput(TransitionLooper &fx, const float *left, const float *right, uint32_t frames,
                            std::vector<float> &mono_out)
{
  renderWithSplitInput(fx, left, right, left, right, frames, mono_out);
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
  std::vector<float> silent_left(bar_frames, 0.f);
  std::vector<float> silent_right(bar_frames, 0.f);
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

  TransitionLooper raw_fx;
  std::vector<float> raw_ram(raw_fx.getBufferSize(), 0.f);
  raw_fx.init(raw_ram.data());
  raw_fx.setTempo(120.f);
  raw_fx.setParameter(TransitionLooper::TIME, 80);
  raw_fx.setParameter(TransitionLooper::MIX, 1000);
  raw_fx.setParameter(TransitionLooper::TYPE, TransitionLooper::TYPE_VOL);
  std::vector<float> muted_left(bar_frames, 0.f);
  std::vector<float> muted_right(bar_frames, 0.f);
  std::vector<float> raw_left(bar_frames, 0.f);
  std::vector<float> raw_right(bar_frames, 0.f);
  fillTone(raw_left, raw_right, 330.f, 0.4f);
  std::vector<float> raw_prime;
  renderWithSplitInput(raw_fx, muted_left.data(), muted_right.data(), raw_left.data(), raw_right.data(), bar_frames,
                       raw_prime);
  const float raw_bypass = windowRms(raw_prime, 8000U, 8000U);
  raw_fx.touchEvent(0, k_unit_touch_phase_began, 512U, 512U);
  std::vector<float> raw_held;
  renderWithInput(raw_fx, silent_left.data(), silent_right.data(), 24000U, raw_held);
  const float raw_loop_rms = windowRms(raw_held, 8000U, 8000U);
  std::printf("nts3_muted_in_bypass=%.6f nts3_raw_loop_rms=%.6f captured=%u\n", raw_bypass, raw_loop_rms,
              raw_fx.capturedSamples());
  if (raw_bypass > 0.01f)
  {
    std::printf("muted unit_render input should stay silent while the pad is up\n");
    return 17;
  }
  if (raw_fx.capturedSamples() < 80000U || raw_loop_rms < 0.05f)
  {
    std::printf("loop should come from get_raw_input even when unit_render input is muted\n");
    return 18;
  }

  TransitionLooper arm_fx;
  std::vector<float> arm_ram(arm_fx.getBufferSize(), 0.f);
  arm_fx.init(arm_ram.data());
  arm_fx.setTempo(120.f);
  arm_fx.setParameter(TransitionLooper::TIME, 80);
  arm_fx.setParameter(TransitionLooper::MIX, 1000);
  arm_fx.setParameter(TransitionLooper::TYPE, TransitionLooper::TYPE_VOL);
  std::vector<float> both_muted_left(bar_frames, 0.f);
  std::vector<float> both_muted_right(bar_frames, 0.f);
  std::vector<float> arm_prime;
  renderWithSplitInput(arm_fx, both_muted_left.data(), both_muted_right.data(), both_muted_left.data(),
                       both_muted_right.data(), bar_frames, arm_prime);
  arm_fx.touchEvent(0, k_unit_touch_phase_began, 512U, 512U);
  if (arm_fx.isFrozen() || !arm_fx.isArming())
  {
    std::printf("silent pre-roll must not freeze; first hold should arm a live capture\n");
    return 19;
  }

  std::vector<float> live_left(bar_frames, 0.f);
  std::vector<float> live_right(bar_frames, 0.f);
  fillTone(live_left, live_right, 196.f, 0.4f);
  std::vector<float> arm_mid;
  renderWithInput(arm_fx, live_left.data(), live_right.data(), 24000U, arm_mid);
  if (arm_fx.isFrozen() || !arm_fx.isArming())
  {
    std::printf("live capture should still be arming before one full bar\n");
    return 20;
  }

  std::vector<float> arm_rest;
  renderWithInput(arm_fx, live_left.data() + 24000U, live_right.data() + 24000U, bar_frames - 24000U, arm_rest);
  std::vector<float> arm_held;
  renderWithInput(arm_fx, silent_left.data(), silent_right.data(), 24000U, arm_held);
  const float armed_loop_rms = windowRms(arm_held, 8000U, 8000U);
  std::printf("armed_frozen=%d armed_wet=%.3f armed_loop_rms=%.6f peak=%.4f\n", arm_fx.isFrozen() ? 1 : 0,
              arm_fx.wetAmount(), armed_loop_rms, arm_fx.capturedPeak());
  if (!arm_fx.isFrozen() || arm_fx.wetAmount() < 0.9f || armed_loop_rms < 0.05f)
  {
    std::printf("after one live bar the hold should freeze and play that capture\n");
    return 21;
  }


  // Nearest-clock snap: tap just after a host 16th → play_pos near step 1.
  TransitionLooper snap_fx;
  std::vector<float> snap_ram(snap_fx.getBufferSize(), 0.f);
  snap_fx.init(snap_ram.data());
  snap_fx.setTempo(120.f);
  snap_fx.setParameter(TransitionLooper::TIME, 80);
  snap_fx.setParameter(TransitionLooper::MIX, 1000);
  snap_fx.setParameter(TransitionLooper::TYPE, TransitionLooper::TYPE_VOL);
  std::vector<float> snap_left(bar_frames, 0.f);
  std::vector<float> snap_right(bar_frames, 0.f);
  fillTone(snap_left, snap_right, 247.f, 0.4f);
  // Establish host grid, then sit slightly after a tick before priming finishes.
  snap_fx.tempo4ppqnTick(1U);
  std::vector<float> snap_prime;
  renderWithInput(snap_fx, snap_left.data(), snap_right.data(), bar_frames, snap_prime);
  snap_fx.tempo4ppqnTick(2U);
  std::vector<float> after_tick(64U, 0.f);
  std::vector<float> after_tick_r(64U, 0.f);
  std::vector<float> after_tick_out;
  renderWithInput(snap_fx, after_tick.data(), after_tick_r.data(), 64U, after_tick_out);
  const float since = snap_fx.debugSamplesSinceTick();
  snap_fx.touchEvent(0, k_unit_touch_phase_began, 512U, 512U);
  const float play_pos = snap_fx.debugPlayPos();
  std::printf("snap_since=%.1f snap_play_pos=%.1f have_tick=%d\n", since, play_pos,
              snap_fx.debugHaveSeenTick() ? 1 : 0);
  if (!snap_fx.debugHaveSeenTick())
  {
    std::printf("host tick should mark the grid before snap\n");
    return 22;
  }
  // 64 samples after the tick at 48 kHz / 120 BPM is far closer to previous than next.
  if (play_pos > 512.f)
  {
    std::printf("late tap after a tick should put play_pos near relative step 1\n");
    return 23;
  }

  // TYPE STEP + PAT H8: with silent live input, only relative steps 1-8 should sound.
  TransitionLooper step_fx;
  std::vector<float> step_ram(step_fx.getBufferSize(), 0.f);
  step_fx.init(step_ram.data());
  step_fx.setTempo(120.f);
  step_fx.setParameter(TransitionLooper::TIME, 80);
  step_fx.setParameter(TransitionLooper::TONE, 1000);
  step_fx.setParameter(TransitionLooper::MIX, 1000);
  step_fx.setParameter(TransitionLooper::TYPE, TransitionLooper::TYPE_STEP);
  step_fx.setParameter(TransitionLooper::PAT, TransitionLooper::PAT_H8);
  std::vector<float> step_left(bar_frames, 0.f);
  std::vector<float> step_right(bar_frames, 0.f);
  fillTone(step_left, step_right, 280.f, 0.4f);
  step_fx.tempo4ppqnTick(1U);
  std::vector<float> step_prime;
  renderWithInput(step_fx, step_left.data(), step_right.data(), bar_frames, step_prime);
  step_fx.tempo4ppqnTick(2U);
  std::vector<float> step_pad(64U, 0.f);
  std::vector<float> step_pad_r(64U, 0.f);
  std::vector<float> step_pad_out;
  renderWithInput(step_fx, step_pad.data(), step_pad_r.data(), 64U, step_pad_out);
  step_fx.touchEvent(0, k_unit_touch_phase_began, 512U, 512U);
  if (step_fx.debugRelativeStep() > 2U)
  {
    std::printf("late tap should land near relative step 1 (got %u)\n", step_fx.debugRelativeStep());
    return 24;
  }

  std::vector<float> step_held;
  renderWithInput(step_fx, silent_left.data(), silent_right.data(), bar_frames, step_held);
  const uint32_t half = bar_frames / 2U;
  const float h8_first = windowRms(step_held, 4000U, half - 8000U);
  const float h8_second = windowRms(step_held, half + 4000U, half - 8000U);
  std::printf("step_h8_first=%.6f step_h8_second=%.6f step=%u\n", h8_first, h8_second,
              step_fx.debugRelativeStep());
  if (h8_first < 0.05f)
  {
    std::printf("H8 should play the frozen loop on relative steps 1-8\n");
    return 25;
  }
  if (h8_second > h8_first * 0.25f + 0.01f)
  {
    std::printf("H8 should stay near silence on relative steps 9-16 when live is silent\n");
    return 26;
  }

  // PAT ALT with silent live: odd steps loud, even steps quiet.
  TransitionLooper alt_fx;
  std::vector<float> alt_ram(alt_fx.getBufferSize(), 0.f);
  alt_fx.init(alt_ram.data());
  alt_fx.setTempo(120.f);
  alt_fx.setParameter(TransitionLooper::TIME, 80);
  alt_fx.setParameter(TransitionLooper::TONE, 1000);
  alt_fx.setParameter(TransitionLooper::MIX, 1000);
  alt_fx.setParameter(TransitionLooper::TYPE, TransitionLooper::TYPE_STEP);
  alt_fx.setParameter(TransitionLooper::PAT, TransitionLooper::PAT_ALT);
  std::vector<float> alt_left(bar_frames, 0.f);
  std::vector<float> alt_right(bar_frames, 0.f);
  fillTone(alt_left, alt_right, 310.f, 0.4f);
  alt_fx.tempo4ppqnTick(1U);
  std::vector<float> alt_prime;
  renderWithInput(alt_fx, alt_left.data(), alt_right.data(), bar_frames, alt_prime);
  alt_fx.tempo4ppqnTick(2U);
  std::vector<float> alt_pad_out;
  renderWithInput(alt_fx, step_pad.data(), step_pad_r.data(), 64U, alt_pad_out);
  alt_fx.touchEvent(0, k_unit_touch_phase_began, 512U, 512U);

  const uint32_t step_frames = bar_frames / 16U;
  std::vector<float> alt_held;
  renderWithInput(alt_fx, silent_left.data(), silent_right.data(), step_frames * 4U, alt_held);
  // Steps 1-2 can start near the freeze "now" (recent pad audio). Use steps 3/4.
  const float alt_odd = windowRms(alt_held, step_frames * 2U + step_frames / 4U, step_frames / 2U);
  const float alt_even = windowRms(alt_held, step_frames * 3U + step_frames / 4U, step_frames / 2U);
  std::printf("step_alt_odd=%.6f step_alt_even=%.6f frozen=%d wet=%.3f\n", alt_odd, alt_even,
              alt_fx.isFrozen() ? 1 : 0, alt_fx.wetAmount());
  if (alt_odd < 0.05f || alt_even > alt_odd * 0.25f + 0.01f)
  {
    std::printf("ALT should sound on odd relative steps and stay quiet on even ones\n");
    return 27;
  }

  // Live steps must pass current input when the mask selects live (L8 first half).
  TransitionLooper live_fx;
  std::vector<float> live_ram(live_fx.getBufferSize(), 0.f);
  live_fx.init(live_ram.data());
  live_fx.setTempo(120.f);
  live_fx.setParameter(TransitionLooper::TIME, 80);
  live_fx.setParameter(TransitionLooper::TONE, 1000);
  live_fx.setParameter(TransitionLooper::MIX, 1000);
  live_fx.setParameter(TransitionLooper::TYPE, TransitionLooper::TYPE_STEP);
  live_fx.setParameter(TransitionLooper::PAT, TransitionLooper::PAT_L8);
  std::vector<float> cap_left(bar_frames, 0.f);
  std::vector<float> cap_right(bar_frames, 0.f);
  fillTone(cap_left, cap_right, 180.f, 0.35f);
  live_fx.tempo4ppqnTick(1U);
  std::vector<float> live_prime;
  renderWithInput(live_fx, cap_left.data(), cap_right.data(), bar_frames, live_prime);
  live_fx.tempo4ppqnTick(2U);
  std::vector<float> live_pad_out;
  renderWithInput(live_fx, step_pad.data(), step_pad_r.data(), 64U, live_pad_out);
  live_fx.touchEvent(0, k_unit_touch_phase_began, 512U, 512U);

  std::vector<float> now_left(half, 0.f);
  std::vector<float> now_right(half, 0.f);
  fillTone(now_left, now_right, 1200.f, 0.45f);
  std::vector<float> live_held;
  renderWithInput(live_fx, now_left.data(), now_right.data(), half, live_held);
  const float l8_live_rms = windowRms(live_held, 4000U, half - 8000U);
  std::printf("step_l8_live_rms=%.6f\n", l8_live_rms);
  if (l8_live_rms < 0.05f)
  {
    std::printf("L8 should pass the current input on relative steps 1-8\n");
    return 28;
  }

  return 0;
}
