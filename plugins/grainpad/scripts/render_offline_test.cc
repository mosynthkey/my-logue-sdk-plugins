#include "grainpad.h"
#include "runtime.h"
#include <cstdio>
#include <vector>

int main()
{
  GrainPad synth;
  synth.init(nullptr);
  synth.setStereoMix(true);
  synth.setParameter(GrainPadEngine::MIX, 1000);
  synth.setParameter(GrainPadEngine::DENS, 700);
  synth.setParameter(GrainPadEngine::SIZE, 512);
  synth.setParameter(GrainPadEngine::SPRAY, 400);

  synth.touchEvent(0, k_unit_touch_phase_began, 512, 768);

  std::vector<float> input(128 * 2, 0.f);
  std::vector<float> output(128 * 2, 0.f);
  float peak = 0.f;
  for (int blockIndex = 0; blockIndex < 200; ++blockIndex)
  {
    synth.process(input.data(), output.data(), 128);
    for (float sample : output)
    {
      const float abs_sample = sample < 0.f ? -sample : sample;
      if (abs_sample > peak)
        peak = abs_sample;
    }
  }

  synth.touchEvent(0, k_unit_touch_phase_ended, 512, 768);
  for (int blockIndex = 0; blockIndex < 100; ++blockIndex)
    synth.process(input.data(), output.data(), 128);

  std::printf("grainpad_offline_peak=%.6f\n", peak);
  return peak > 0.001f ? 0 : 1;
}
