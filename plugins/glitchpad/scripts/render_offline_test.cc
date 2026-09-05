#include "glitchpad.h"
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

static void renderWithSplitInput(GlitchPad &fx, const float *dry_left, const float *dry_right,
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

static void renderWithInput(GlitchPad &fx, const float *left, const float *right, uint32_t frames,
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

static void setup(GlitchPad &fx, int32_t mode)
{
  fx.setTempo(120.f);
  fx.setParameter(GlitchPad::MODE, mode);
  fx.setParameter(GlitchPad::TIME, 563);
  fx.setParameter(GlitchPad::MIX, 1000);
  fx.setParameter(GlitchPad::DECAY, 256);
  fx.setParameter(GlitchPad::CRUSH, 0);
  fx.setParameter(GlitchPad::SYNC, GlitchPad::SYNC_EVEN);
  fx.setParameter(GlitchPad::HOLD, GlitchPad::HOLD_GATE);
}

int main()
{
  GlitchPad fx;
  std::vector<float> ram(fx.getBufferSize(), 0.f);
  fx.init(ram.data());
  setup(fx, GlitchPad::MODE_RTRG);

  const uint32_t bar_frames = 96000U;
  std::vector<float> left(bar_frames, 0.f);
  std::vector<float> right(bar_frames, 0.f);
  fillTone(left, right, 220.f, 0.4f);
  std::vector<float> silent_left(24000U, 0.f);
  std::vector<float> silent_right(24000U, 0.f);

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
  std::printf("captured_after_bar=%u\n", fx.capturedSamples());
  if (fx.capturedSamples() < 80000U)
  {
    std::printf("expected a nearly full bar of capture before tap\n");
    return 11;
  }

  fx.touchEvent(0, k_unit_touch_phase_began, 64U, 700U);
  std::vector<float> held;
  renderWithInput(fx, silent_left.data(), silent_right.data(), 24000U, held);
  const float loop_rms = windowRms(held, 8000U, 8000U);
  std::printf("rtrg_rms=%.6f active=%d wet=%.3f slice=%u\n", loop_rms, fx.isActive() ? 1 : 0, fx.wetAmount(),
              fx.sliceLength());
  if (loop_rms < 0.05f || !fx.isActive() || fx.wetAmount() < 0.9f)
  {
    std::printf("held retrigger should keep playing the captured slice after input goes silent\n");
    return 12;
  }

  fx.touchEvent(0, k_unit_touch_phase_ended, 64U, 700U);
  std::vector<float> released;
  renderWithInput(fx, silent_left.data(), silent_right.data(), 24000U, released);
  const float release_tail = windowRms(released, 20000U, 3000U);
  std::printf("release_tail=%.6f active=%d\n", release_tail, fx.isActive() ? 1 : 0);
  if (release_tail > 0.01f || fx.isActive())
  {
    std::printf("GATE release should return to silence when the input is silent\n");
    return 13;
  }

  GlitchPad rev_fx;
  std::vector<float> rev_ram(rev_fx.getBufferSize(), 0.f);
  rev_fx.init(rev_ram.data());
  setup(rev_fx, GlitchPad::MODE_REV);
  std::vector<float> rev_prime;
  renderWithInput(rev_fx, left.data(), right.data(), bar_frames, rev_prime);
  rev_fx.touchEvent(0, k_unit_touch_phase_began, 200U, 700U);
  std::vector<float> rev_held;
  renderWithInput(rev_fx, silent_left.data(), silent_right.data(), 24000U, rev_held);
  const float rev_rms = windowRms(rev_held, 8000U, 8000U);
  std::printf("rev_rms=%.6f\n", rev_rms);
  if (rev_rms < 0.05f)
  {
    std::printf("reverse scene should play the frozen slice\n");
    return 14;
  }

  GlitchPad gate_fx;
  std::vector<float> gate_ram(gate_fx.getBufferSize(), 0.f);
  gate_fx.init(gate_ram.data());
  setup(gate_fx, GlitchPad::MODE_GATE);
  gate_fx.setParameter(GlitchPad::TIME, 400);
  gate_fx.setParameter(GlitchPad::DECAY, 0);
  gate_fx.touchEvent(0, k_unit_touch_phase_began, 700U, 400U);
  std::vector<float> gated;
  renderWithInput(gate_fx, left.data(), right.data(), 24000U, gated);
  const float gate_high = windowRms(gated, 2000U, 6000U);
  const float gate_low = windowRms(gated, 14000U, 6000U);
  std::printf("gate_high=%.6f gate_low=%.6f slice=%u\n", gate_high, gate_low, gate_fx.sliceLength());
  if (gate_high < 0.05f || gate_low > gate_high * 0.75f)
  {
    std::printf("gate scene should chop the live input\n");
    return 15;
  }

  GlitchPad crush_fx;
  std::vector<float> crush_ram(crush_fx.getBufferSize(), 0.f);
  crush_fx.init(crush_ram.data());
  setup(crush_fx, GlitchPad::MODE_CRUSH);
  crush_fx.setParameter(GlitchPad::TIME, 900);
  crush_fx.touchEvent(0, k_unit_touch_phase_began, 900U, 900U);
  std::vector<float> crushed;
  renderWithInput(crush_fx, left.data(), right.data(), 8000U, crushed);
  const float crush_rms = windowRms(crushed, 2000U, 4000U);
  std::printf("crush_rms=%.6f\n", crush_rms);
  if (crush_rms < 0.05f)
  {
    std::printf("crush scene should still pass audible audio\n");
    return 16;
  }

  GlitchPad raw_fx;
  std::vector<float> raw_ram(raw_fx.getBufferSize(), 0.f);
  raw_fx.init(raw_ram.data());
  setup(raw_fx, GlitchPad::MODE_RTRG);
  std::vector<float> muted_left(bar_frames, 0.f);
  std::vector<float> muted_right(bar_frames, 0.f);
  std::vector<float> raw_left(bar_frames, 0.f);
  std::vector<float> raw_right(bar_frames, 0.f);
  fillTone(raw_left, raw_right, 330.f, 0.4f);
  std::vector<float> raw_prime;
  renderWithSplitInput(raw_fx, muted_left.data(), muted_right.data(), raw_left.data(), raw_right.data(), bar_frames,
                       raw_prime);
  const float raw_bypass = windowRms(raw_prime, 8000U, 8000U);
  raw_fx.touchEvent(0, k_unit_touch_phase_began, 64U, 700U);
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
    std::printf("retrigger should come from get_raw_input even when unit_render input is muted\n");
    return 18;
  }

  GlitchPad arm_fx;
  std::vector<float> arm_ram(arm_fx.getBufferSize(), 0.f);
  arm_fx.init(arm_ram.data());
  setup(arm_fx, GlitchPad::MODE_RTRG);
  std::vector<float> both_muted_left(bar_frames, 0.f);
  std::vector<float> both_muted_right(bar_frames, 0.f);
  std::vector<float> arm_prime;
  renderWithSplitInput(arm_fx, both_muted_left.data(), both_muted_right.data(), both_muted_left.data(),
                       both_muted_right.data(), bar_frames, arm_prime);
  arm_fx.touchEvent(0, k_unit_touch_phase_began, 64U, 700U);
  if (arm_fx.isActive() && !arm_fx.isArming())
  {
    std::printf("silent pre-roll must not engage a buffer scene; first hold should arm\n");
    return 19;
  }

  std::vector<float> live_left(bar_frames, 0.f);
  std::vector<float> live_right(bar_frames, 0.f);
  fillTone(live_left, live_right, 196.f, 0.4f);
  std::vector<float> arm_live;
  renderWithInput(arm_fx, live_left.data(), live_right.data(), 24000U, arm_live);
  const float armed_rms = windowRms(arm_live, 8000U, 8000U);
  std::printf("armed_active=%d armed_arming=%d armed_rms=%.6f peak=%.4f\n", arm_fx.isActive() ? 1 : 0,
              arm_fx.isArming() ? 1 : 0, armed_rms, arm_fx.capturedPeak());
  if (!arm_fx.isActive() || arm_fx.isArming() || armed_rms < 0.05f)
  {
    std::printf("after a live slice arrives the hold should freeze and retrigger\n");
    return 20;
  }

  GlitchPad latch_fx;
  std::vector<float> latch_ram(latch_fx.getBufferSize(), 0.f);
  latch_fx.init(latch_ram.data());
  setup(latch_fx, GlitchPad::MODE_RTRG);
  latch_fx.setParameter(GlitchPad::HOLD, GlitchPad::HOLD_LATCH);
  std::vector<float> latch_prime;
  renderWithInput(latch_fx, left.data(), right.data(), bar_frames, latch_prime);
  latch_fx.touchEvent(0, k_unit_touch_phase_began, 64U, 700U);
  std::vector<float> latch_held;
  renderWithInput(latch_fx, silent_left.data(), silent_right.data(), 4000U, latch_held);
  latch_fx.touchEvent(0, k_unit_touch_phase_ended, 64U, 700U);
  std::vector<float> latch_after;
  renderWithInput(latch_fx, silent_left.data(), silent_right.data(), 12000U, latch_after);
  const float latch_rms = windowRms(latch_after, 4000U, 6000U);
  std::printf("latch_rms=%.6f active=%d held=%d\n", latch_rms, latch_fx.isActive() ? 1 : 0,
              latch_fx.isPadHeld() ? 1 : 0);
  if (!latch_fx.isActive() || latch_rms < 0.05f)
  {
    std::printf("LATCH should keep retriggering after the pad is released\n");
    return 21;
  }

  static const int32_t kBufferModes[] = {GlitchPad::MODE_SHUF, GlitchPad::MODE_TAPE, GlitchPad::MODE_STRCH};
  for (uint32_t modeIndex = 0; modeIndex < 3U; ++modeIndex)
  {
    const int32_t mode = kBufferModes[modeIndex];
    GlitchPad mode_fx;
    std::vector<float> mode_ram(mode_fx.getBufferSize(), 0.f);
    mode_fx.init(mode_ram.data());
    setup(mode_fx, mode);
    std::vector<float> mode_prime;
    renderWithInput(mode_fx, left.data(), right.data(), bar_frames, mode_prime);
    mode_fx.touchEvent(0, k_unit_touch_phase_began, 300U, 600U);
    std::vector<float> mode_held;
    renderWithInput(mode_fx, silent_left.data(), silent_right.data(), 12000U, mode_held);
    const float mode_rms = windowRms(mode_held, 2000U, 4000U);
    std::printf("mode_%d_rms=%.6f\n", mode, mode_rms);
    if (mode_rms < 0.02f)
    {
      std::printf("buffer scene %d should play captured audio\n", mode);
      return 23;
    }
  }

  GlitchPad delay_fx;
  std::vector<float> delay_ram(delay_fx.getBufferSize(), 0.f);
  delay_fx.init(delay_ram.data());
  setup(delay_fx, GlitchPad::MODE_DLY);
  delay_fx.setParameter(GlitchPad::TIME, 700);
  delay_fx.setParameter(GlitchPad::DECAY, 700);
  delay_fx.touchEvent(0, k_unit_touch_phase_began, 1000U, 700U);
  std::vector<float> delay_burst(24000U, 0.f);
  std::vector<float> delay_burst_r(24000U, 0.f);
  fillTone(delay_burst, delay_burst_r, 440.f, 0.5f);
  for (uint32_t sampleIndex = 4000U; sampleIndex < delay_burst.size(); ++sampleIndex)
  {
    delay_burst[sampleIndex] = 0.f;
    delay_burst_r[sampleIndex] = 0.f;
  }
  std::vector<float> delayed;
  renderWithInput(delay_fx, delay_burst.data(), delay_burst_r.data(), 24000U, delayed);
  const float delay_echo = windowRms(delayed, 12000U, 4000U);
  std::printf("delay_echo=%.6f\n", delay_echo);
  if (delay_echo < 0.02f)
  {
    std::printf("delay scene should keep repeating after the input burst\n");
    return 24;
  }

  latch_fx.setParameter(GlitchPad::HOLD, GlitchPad::HOLD_GATE);
  std::vector<float> latch_off;
  renderWithInput(latch_fx, silent_left.data(), silent_right.data(), 24000U, latch_off);
  const float latch_off_rms = windowRms(latch_off, 20000U, 3000U);
  std::printf("latch_off_rms=%.6f active=%d\n", latch_off_rms, latch_fx.isActive() ? 1 : 0);
  if (latch_off_rms > 0.01f || latch_fx.isActive())
  {
    std::printf("switching HOLD back to GATE with the pad up should stop the effect\n");
    return 22;
  }

  return 0;
}
