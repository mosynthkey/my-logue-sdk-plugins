#include "drums.h"
#include "macros.h"
#include "runtime.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static void render(Drums &kit, std::vector<float> &mono, uint32_t frames)
{
  std::vector<float> block(frames * 2U, 0.f);
  kit.process(block.data(), block.data(), frames);
  for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    mono.push_back(block[sampleIndex * 2U]);
}

static float peakAbs(const std::vector<float> &mono)
{
  float peak = 0.f;
  for (float sample : mono)
  {
    const float magnitude = std::fabs(sample);
    if (magnitude > peak)
      peak = magnitude;
  }
  return peak;
}

static void setup(Drums &kit)
{
  kit.init(nullptr);
  kit.setParameter(Drums::MIX, 1000);
  kit.setParameter(Drums::DENS, 450);
  kit.setParameter(Drums::FILL, 200);
  kit.setParameter(Drums::GENRE, Drums::GENRE_TRANCE);
  kit.setParameter(Drums::KIT, Drums::KIT_AUTO);
  kit.setParameter(Drums::SWING, 200);
  kit.setParameter(Drums::TONE, 460);
  kit.setParameter(Drums::DEC, 450);
  kit.setTempo(138.f);
}

int main()
{
  Drums kit;
  setup(kit);

  if (std::strcmp(kit.getParameterStrValue(Drums::KIT, Drums::KIT_AUTO), "AUTO") != 0 ||
      std::strcmp(kit.getParameterStrValue(Drums::KIT, Drums::KIT_BOOM), "BOOM") != 0)
  {
    std::printf("kit strings missing\n");
    return 1;
  }

  // AUTO follows GENRE; explicit KIT overrides voice feel index.
  kit.setParameter(Drums::GENRE, Drums::GENRE_FOOT);
  kit.setParameter(Drums::KIT, Drums::KIT_AUTO);
  if (kit.debugActiveKit() != Drums::GENRE_FOOT)
  {
    std::printf("AUTO kit mismatch: %d\n", kit.debugActiveKit());
    return 1;
  }
  kit.setParameter(Drums::KIT, Drums::KIT_BOOM);
  if (kit.debugActiveKit() != Drums::GENRE_BOOM)
  {
    std::printf("explicit kit mismatch: %d\n", kit.debugActiveKit());
    return 1;
  }

  // Corner Fill: one snare on every 16th of the bar.
  setup(kit);
  kit.debugResetCounters();
  kit.touchEvent(0, k_unit_touch_phase_began, 900U, 900U);
  if (kit.debugFillTimer() != Drums::kSteps)
  {
    std::printf("corner fill not armed: %u\n", kit.debugFillTimer());
    return 1;
  }

  uint32_t snare_like = 0U;
  for (uint32_t stepIndex = 0; stepIndex < Drums::kSteps; ++stepIndex)
  {
    const uint32_t before = kit.debugMainTriggers();
    kit.debugEmitStep(stepIndex);
    if (kit.debugMainTriggers() > before)
      ++snare_like;
  }
  // Corner fill fires a snare every step (+ kick anchors). Expect dense mains.
  if (snare_like < 16U)
  {
    std::printf("corner fill too sparse: hits=%u\n", snare_like);
    return 1;
  }
  if (kit.debugFillTimer() != 0U)
  {
    std::printf("fill timer not consumed: %u\n", kit.debugFillTimer());
    return 1;
  }

  // Slide into corner while held also arms Fill.
  setup(kit);
  kit.touchEvent(0, k_unit_touch_phase_began, 200U, 200U);
  if (kit.debugFillTimer() != 0U)
  {
    std::printf("center touch armed fill: %u\n", kit.debugFillTimer());
    return 1;
  }
  kit.touchEvent(0, k_unit_touch_phase_moved, 900U, 900U);
  if (kit.debugFillTimer() != Drums::kSteps)
  {
    std::printf("slide into corner missed fill: %u\n", kit.debugFillTimer());
    return 1;
  }

  // Audio: corner-fill bar should be clearly audible.
  setup(kit);
  kit.debugForceRun();
  kit.touchEvent(0, k_unit_touch_phase_began, 900U, 900U);
  std::vector<float> mono;
  const uint32_t bar_samples = static_cast<uint32_t>(48000.f * 60.f / (138.f * 4.f) * 16.f);
  render(kit, mono, bar_samples);
  const float peak = peakAbs(mono);
  if (peak < 0.08f)
  {
    std::printf("corner fill peak too quiet: %f\n", peak);
    return 1;
  }

  std::printf("ok peak=%f mains=%u kit=%d\n", peak, kit.debugMainTriggers(), kit.debugActiveKit());
  return 0;
}
