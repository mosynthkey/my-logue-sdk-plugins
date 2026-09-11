#include "kopunch.h"

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

static void renderMode(KoPunch &fx, int32_t mode, const std::vector<float> &left,
                       const std::vector<float> &right, std::vector<float> &mono_out)
{
  fx.reset();
  fx.setTempo(120.f);
  fx.setParameter(KoPunch::MODE, mode);
  fx.setParameter(KoPunch::PRESS, 700);
  fx.setParameter(KoPunch::MIX, 1000);
  fx.setParameter(KoPunch::COLOR, 410);
  fx.setParameter(KoPunch::HOLD, KoPunch::HOLD_GATE);

  const uint32_t preroll = 48000U * 2U;
  const uint32_t frames = static_cast<uint32_t>(left.size());
  std::vector<float> in_block(128U * 2U, 0.f);
  std::vector<float> out_block(128U * 2U, 0.f);

  // Pre-roll capture while pad is up.
  for (uint32_t frameOffset = 0; frameOffset < preroll; frameOffset += 64U)
  {
    for (uint32_t sampleIndex = 0; sampleIndex < 64U; ++sampleIndex)
    {
      const uint32_t sourceIndex = (frameOffset + sampleIndex) % frames;
      in_block[sampleIndex * 2U] = left[sourceIndex];
      in_block[sampleIndex * 2U + 1U] = right[sourceIndex];
    }
    fx.process(in_block.data(), in_block.data(), out_block.data(), 64U);
  }

  fx.touchEvent(0, k_unit_touch_phase_began, 512U, 700U);
  mono_out.clear();
  mono_out.reserve(frames);

  for (uint32_t frameOffset = 0; frameOffset < frames; frameOffset += 64U)
  {
    const uint32_t this_block = (frames - frameOffset) > 64U ? 64U : (frames - frameOffset);
    for (uint32_t sampleIndex = 0; sampleIndex < this_block; ++sampleIndex)
    {
      const uint32_t sourceIndex = frameOffset + sampleIndex;
      in_block[sampleIndex * 2U] = left[sourceIndex];
      in_block[sampleIndex * 2U + 1U] = right[sourceIndex];
    }
    fx.process(in_block.data(), in_block.data(), out_block.data(), this_block);
    for (uint32_t sampleIndex = 0; sampleIndex < this_block; ++sampleIndex)
      mono_out.push_back(out_block[sampleIndex * 2U]);
  }
}

int main()
{
  const uint32_t frames = 48000U * 3U;
  std::vector<float> left(frames, 0.f);
  std::vector<float> right(frames, 0.f);
  // Broadband-ish stack so HPF still has energy when cutoff rises.
  fillTone(left, right, 220.f, 0.18f);
  {
    std::vector<float> high_left(frames, 0.f);
    std::vector<float> high_right(frames, 0.f);
    fillTone(high_left, high_right, 2500.f, 0.22f);
    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      left[sampleIndex] += high_left[sampleIndex];
      right[sampleIndex] += high_right[sampleIndex];
    }
  }

  const uint32_t buffer_floats = KoPunch().getBufferSize();
  std::vector<float> sdram(buffer_floats, 0.f);
  KoPunch fx;
  fx.init(sdram.data());

  static const char *names[KoPunch::NUM_MODES] = {
      "PRND", "SWAP", "GRAN", "RPT", "TAPE", "FLFO",
      "LPF", "HPF", "SEND", "TREM", "OCTD", "DEC"};

  int failures = 0;
  for (int32_t mode = 0; mode < KoPunch::NUM_MODES; ++mode)
  {
    std::vector<float> mono;
    renderMode(fx, mode, left, right, mono);
    const float early = windowRms(mono, 1000U, 2000U);
    const float mid = windowRms(mono, 20000U, 4000U);
    const bool silent = early < 0.001f && mid < 0.001f;
    std::printf("mode %2d %-4s early=%.4f mid=%.4f %s\n", mode, names[mode], early, mid,
                silent ? "FAIL" : "ok");
    if (silent)
      ++failures;

    // Tape stop at PRESS≈0.68 lasts ~1.4 s; require clear decay by 2 s.
    if (mode == KoPunch::MODE_TAPE)
    {
      const float late = windowRms(mono, 96000U, 4000U);
      if (!(late < early * 0.25f))
      {
        std::printf("  TAPE decay check failed (late=%.4f early=%.4f)\n", late, early);
        ++failures;
      }
    }
  }

  fx.teardown();
  if (failures != 0)
  {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("all modes produce audio\n");
  return 0;
}
