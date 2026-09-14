// Quick host-side probe: rub motion should yield non-silent audio with a body.
#include "stoneres.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main()
{
  StoneRes unit;
  std::vector<float> ram(unit.getBufferSize(), 0.f);
  unit.init(ram.data());
  unit.setParameter(StoneRes::LOAD, 700);
  unit.setParameter(StoneRes::ROUGH, 600);
  unit.setParameter(StoneRes::MIX, 800);
  unit.setParameter(StoneRes::MAT, 0);
  unit.setParameter(StoneRes::GRAIN, 500);
  unit.setParameter(StoneRes::DAMP, 600);
  unit.setParameter(StoneRes::ROOT, 0);
  unit.setParameter(StoneRes::CHORD, StoneRes::CHORD_MAJ7);

  std::vector<float> in(128 * 2, 0.f);
  std::vector<float> out(128 * 2, 0.f);

  unit.touchEvent(0, k_unit_touch_phase_began, 100, 100);
  float peak = 0.f;
  float energy_20ms = 0.f;
  float energy_80ms = 0.f;
  const uint32_t total_frames = 48000 / 5; // 200 ms
  uint32_t x = 100;
  uint32_t y = 100;
  for (uint32_t frame = 0; frame < total_frames; frame += 128)
  {
    // Simulate continuous rub.
    x = 100 + ((frame / 16) % 400);
    y = 100 + (((frame / 16) * 3) % 400);
    unit.touchEvent(0, k_unit_touch_phase_moved, x, y);
    unit.process(in.data(), out.data(), 128);
    for (uint32_t sampleIndex = 0; sampleIndex < 128; ++sampleIndex)
    {
      const float sample = std::fabs(out[sampleIndex * 2]);
      if (sample > peak)
        peak = sample;
      const uint32_t abs_frame = frame + sampleIndex;
      if (abs_frame < 960)
        energy_20ms += sample;
      if (abs_frame >= 3840 && abs_frame < 4800)
        energy_80ms += sample;
    }
  }

  std::printf("peak=%.4f energy20=%.4f energy80=%.4f\n", peak, energy_20ms, energy_80ms);
  if (peak < 0.01f)
  {
    std::fprintf(stderr, "FAIL: output nearly silent\n");
    return 1;
  }
  if (energy_80ms < 0.5f)
  {
    std::fprintf(stderr, "FAIL: body died too fast (HSnare-class)\n");
    return 2;
  }
  std::printf("OK\n");
  return 0;
}
