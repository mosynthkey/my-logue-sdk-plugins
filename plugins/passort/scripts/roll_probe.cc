/*
 * Host-side probe for Passort region lock and beat-sync roll energy.
 */
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

enum
{
  MODE_NONE = 0,
  MODE_HPF,
  MODE_LPF,
  MODE_TAPE,
  MODE_DELAY,
  MODE_ROLL
};

static uint8_t modeFromStart(uint32_t x, uint32_t y)
{
  const float kCenterRadius = 190.f;
  const float dx = static_cast<float>(x) - 511.5f;
  const float dy = static_cast<float>(y) - 511.5f;
  if (dx * dx + dy * dy <= kCenterRadius * kCenterRadius)
    return MODE_TAPE;

  const bool left = x < 512U;
  const bool top = y >= 512U;
  if (left && top)
    return MODE_HPF;
  if (!left && top)
    return MODE_LPF;
  if (left && !top)
    return MODE_DELAY;
  return MODE_ROLL;
}

static float rollBufferBeats(float len_norm)
{
  static const float kBeats[5] = {0.0625f, 0.125f, 0.25f, 0.5f, 1.f};
  const float select = (len_norm < 0.f ? 0.f : len_norm > 1.f ? 1.f : len_norm) * 4.0001f;
  uint32_t step = static_cast<uint32_t>(select);
  if (step > 4U)
    step = 4U;
  return kBeats[step];
}

static uint32_t rollSpeedDiv(float speed_norm)
{
  static const uint32_t kDivs[5] = {1U, 2U, 4U, 8U, 16U};
  const float select = (speed_norm < 0.f ? 0.f : speed_norm > 1.f ? 1.f : speed_norm) * 4.0001f;
  uint32_t step = static_cast<uint32_t>(select);
  if (step > 4U)
    step = 4U;
  return kDivs[step];
}

static uint32_t computeLoop(float speed, float len, float bpm, float sr)
{
  const float beat = sr * 60.f / bpm;
  const float buf = rollBufferBeats(len);
  const uint32_t div = rollSpeedDiv(speed);
  uint32_t samples = static_cast<uint32_t>(buf * beat / static_cast<float>(div) + 0.5f);
  if (samples < 64U)
    samples = 64U;
  return samples;
}

int main()
{
  int failures = 0;

  if (modeFromStart(900, 100) != MODE_ROLL)
  {
    std::printf("FAIL bottom-right should be ROLL\n");
    ++failures;
  }
  else
  {
    std::printf("OK bottom-right -> ROLL\n");
  }

  // Corner start: short buffer, slow speed -> 1/16 beat at 120 BPM / 48k = 2500
  const uint32_t corner = computeLoop(0.f, 0.f, 120.f, 48000.f);
  const uint32_t expect_corner = 1500U; // 0.0625 * 24000
  if (corner != expect_corner)
  {
    std::printf("FAIL corner loop=%u want %u\n", corner, expect_corner);
    ++failures;
  }
  else
  {
    std::printf("OK corner loop=%u (1/16 beat)\n", corner);
  }

  // Move up: faster -> 1/16 / 16 = 1/256 beat clamped to 64
  const uint32_t fast = computeLoop(1.f, 0.f, 120.f, 48000.f);
  if (fast < 64U || fast > 150U)
  {
    std::printf("FAIL fast loop=%u want ~94\n", fast);
    ++failures;
  }
  else
  {
    std::printf("OK fast/short loop=%u (1/256 beat)\n", fast);
  }

  // Move left: longer buffer, slow -> 1 beat
  const uint32_t long_slow = computeLoop(0.f, 1.f, 120.f, 48000.f);
  if (long_slow != 24000U)
  {
    std::printf("FAIL long loop=%u want 24000\n", long_slow);
    ++failures;
  }
  else
  {
    std::printf("OK long/slow loop=%u (1 beat)\n", long_slow);
  }

  // Simulate a frozen ring roll and check energy is preserved.
  const uint32_t loop = 2500U;
  std::vector<float> buf(loop * 2U);
  for (uint32_t sampleIndex = 0; sampleIndex < loop; ++sampleIndex)
  {
    const float phase = static_cast<float>(sampleIndex) * (2.f * 3.14159265f * 8.f / loop);
    buf[sampleIndex] = std::sin(phase);
    buf[loop + sampleIndex] = buf[sampleIndex];
  }

  double energy = 0.0;
  float pos = 0.f;
  for (uint32_t sampleIndex = 0; sampleIndex < loop * 4U; ++sampleIndex)
  {
    const uint32_t index = static_cast<uint32_t>(pos) % loop;
    energy += static_cast<double>(buf[index] * buf[index]);
    pos += 1.f;
    if (pos >= static_cast<float>(loop))
      pos -= static_cast<float>(loop);
  }
  const double rms = std::sqrt(energy / (loop * 4.0));
  if (rms < 0.5)
  {
    std::printf("FAIL roll rms=%.3f too quiet\n", rms);
    ++failures;
  }
  else
  {
    std::printf("OK roll rms=%.3f over 4 loops\n", rms);
  }

  if (failures != 0)
  {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("all roll probes passed\n");
  return 0;
}
