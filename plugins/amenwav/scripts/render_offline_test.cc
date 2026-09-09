#include "amenwav.h"
#include "macros.h"
#include "runtime.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static void setup(AmenWav &fx, float bpm)
{
  fx.init(nullptr);
  fx.setParameter(AmenWav::MIX, 1000);
  fx.setParameter(AmenWav::STRT, 0);
  fx.setParameter(AmenWav::SIZE, 640);
  fx.setParameter(AmenWav::TUNE, 512);
  fx.setParameter(AmenWav::RPT, 0);
  fx.setParameter(AmenWav::REVS, 0);
  fx.setParameter(AmenWav::HOLD, 0);
  fx.setTempo(bpm);
}

int main()
{
  AmenWav fx;
  setup(fx, 120.f);
  fx.touchEvent(0, k_unit_touch_phase_began, 0, 640);

  std::vector<float> block(128U * 2U, 0.f);
  float peak = 0.f;
  for (uint32_t blockIndex = 0; blockIndex < 750U; ++blockIndex)
  {
    fx.process(block.data(), block.data(), 128U);
    for (uint32_t sampleIndex = 0; sampleIndex < 256U; ++sampleIndex)
    {
      const float magnitude = std::fabs(block[sampleIndex]);
      if (magnitude > peak)
        peak = magnitude;
    }
  }

  std::printf("wav_placeholder peak=%.4f triggers=%u\n", peak, fx.debugTriggerCount());
  if (peak < 0.08f || peak > 0.99f)
    return 1;
  if (fx.debugTriggerCount() < 15U)
    return 2;
  std::printf("ok\n");
  return 0;
}
