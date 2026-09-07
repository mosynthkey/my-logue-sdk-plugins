#include "grainpad.h"
#include "runtime.h"
#include <cstdio>
#include <vector>

int main()
{
  constexpr uint32_t kFrames = 128U;
  GrainPad fx;
  std::vector<float> ram(fx.getBufferSize(), 0.f);
  fx.init(ram.data());
  fx.setTempo(120.f);
  fx.setParameter(GrainPad::MIX, 1000);
  fx.setParameter(GrainPad::FEEL, 800);
  fx.setParameter(GrainPad::OCT, 700);
  fx.setParameter(GrainPad::ENV, 600);
  fx.setParameter(GrainPad::SYNC, 1);
  fx.setParameter(GrainPad::SPRD, 700);
  fx.setParameter(GrainPad::HPF, 200);
  fx.setParameter(GrainPad::REVS, 200);

  std::vector<float> input(kFrames * 2U, 0.f);
  std::vector<float> output(kFrames * 2U, 0.f);

  for (int blockIndex = 0; blockIndex < 400; ++blockIndex)
  {
    for (uint32_t sampleIndex = 0; sampleIndex < kFrames; ++sampleIndex)
    {
      const float phase = static_cast<float>((blockIndex * static_cast<int>(kFrames) + static_cast<int>(sampleIndex)) % 96) / 96.f;
      const float sample = (phase < 0.5f ? phase * 4.f - 1.f : 3.f - phase * 4.f) * 0.25f;
      input[sampleIndex * 2U] = sample;
      input[sampleIndex * 2U + 1U] = sample * 0.85f;
    }
    fx.process(input.data(), input.data(), output.data(), kFrames);
  }

  fx.touchEvent(0, k_unit_touch_phase_began, 800, 512);

  float peak = 0.f;
  for (int blockIndex = 0; blockIndex < 300; ++blockIndex)
  {
    fx.process(input.data(), input.data(), output.data(), kFrames);
    for (float sample : output)
    {
      const float abs_sample = sample < 0.f ? -sample : sample;
      if (abs_sample > peak)
        peak = abs_sample;
    }
  }

  fx.touchEvent(0, k_unit_touch_phase_ended, 800, 512);
  for (int blockIndex = 0; blockIndex < 100; ++blockIndex)
    fx.process(input.data(), input.data(), output.data(), kFrames);

  std::printf("grainpad_offline_peak=%.6f\n", peak);
  return peak > 0.001f ? 0 : 1;
}
