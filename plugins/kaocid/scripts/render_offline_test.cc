#include "kaocid.h"
#include "macros.h"
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

static void renderBlock(Kaocid &synth, uint32_t frames, std::vector<float> &mono_accum)
{
  std::vector<float> block(frames * 2U, 0.f);
  synth.process(block.data(), block.data(), frames);
  for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    mono_accum.push_back(block[sampleIndex * 2U]);
}

static void setup(Kaocid &synth)
{
  synth.init(nullptr);
  synth.setParameter(Kaocid::MIX, 1000);
  synth.setParameter(Kaocid::CUT, 700);
  synth.setParameter(Kaocid::RES, 700);
  synth.setParameter(Kaocid::ENV, 614);
  synth.setParameter(Kaocid::DEC, 410);
  synth.setParameter(Kaocid::ACC, 563);
  synth.setParameter(Kaocid::ROOT, 36);
  synth.setTempo(120.f);
}

int main()
{
  Kaocid synth;
  for (uint32_t seedIndex = 0; seedIndex < 8U; ++seedIndex)
  {
    setup(synth);
    synth.touchEvent(0, k_unit_touch_phase_began, 80U + seedIndex * 90U, 200U + seedIndex * 70U);
    if (synth.debugGlideCount() < Kaocid::kMinGlidesPerPhrase)
    {
      std::printf("seed %u has too few glides (%u)\n", seedIndex, synth.debugGlideCount());
      return 10;
    }
  }

  setup(synth);
  synth.touchEvent(0, k_unit_touch_phase_began, 512, 512);

  const uint32_t glide_count = synth.debugGlideCount();
  std::printf("glide_count=%u phrase=", glide_count);
  for (uint32_t stepIndex = 0; stepIndex < Kaocid::kStepsPerBar; ++stepIndex)
  {
    const int degree = static_cast<int>(synth.debugDegree(stepIndex));
    std::printf("%s%d%s", (stepIndex == 0U) ? "" : " ", degree, synth.debugSlide(stepIndex) ? "s" : "");
  }
  std::printf("\n");

  if (glide_count < Kaocid::kMinGlidesPerPhrase)
  {
    std::printf("phrase has too few glides\n");
    return 10;
  }

  uint32_t slide_source = Kaocid::kStepsPerBar;
  for (uint32_t stepIndex = 0; stepIndex < Kaocid::kStepsPerBar; ++stepIndex)
  {
    const uint32_t next_index = (stepIndex + 1U) % Kaocid::kStepsPerBar;
    if (synth.debugSlide(stepIndex) && synth.debugDegree(stepIndex) >= 0 &&
        synth.debugDegree(next_index) >= 0)
    {
      slide_source = stepIndex;
      break;
    }
  }
  if (slide_source >= Kaocid::kStepsPerBar)
  {
    std::printf("no slide into a destination note\n");
    return 11;
  }

  const uint32_t slide_dest = (slide_source + 1U) % Kaocid::kStepsPerBar;
  const float source_pitch = 36.f + static_cast<float>(synth.debugDegree(slide_source));
  const float dest_pitch = 36.f + static_cast<float>(synth.debugDegree(slide_dest));
  float interval = dest_pitch - source_pitch;
  if (interval < 0.f)
    interval = -interval;
  std::printf("slide %u->%u  %.1f->%.1f  interval=%.1f\n", slide_source, slide_dest, source_pitch,
              dest_pitch, interval);

  const uint32_t samples_per_step = 6000U;
  const uint32_t slide_start = (slide_source + 1U) * samples_per_step;
  std::vector<float> mono;
  mono.reserve(slide_start + 4000U);

  bool saw_slide_flag = false;
  float pitch_at_start = 0.f;
  float pitch_at_30ms = 0.f;
  float max_progress = 0.f;

  uint32_t sample_count = 0U;
  while (sample_count < slide_start + 3600U)
  {
    renderBlock(synth, 64U, mono);
    sample_count += 64U;
    if (sample_count >= slide_start && sample_count < slide_start + 64U)
    {
      saw_slide_flag = synth.debugSlideActive();
      pitch_at_start = synth.debugPitch();
    }
    if (sample_count >= slide_start + 1440U && pitch_at_30ms == 0.f)
      pitch_at_30ms = synth.debugPitch();

    if (sample_count >= slide_start && interval > 0.1f)
    {
      float progress = (synth.debugPitch() - source_pitch) / (dest_pitch - source_pitch);
      if (progress < 0.f)
        progress = -progress;
      if (progress > max_progress)
        max_progress = progress;
    }
  }

  std::printf("slide_active_at_boundary=%d pitch_start=%.3f pitch_30ms=%.3f max_progress=%.3f\n",
              saw_slide_flag ? 1 : 0, pitch_at_start, pitch_at_30ms, max_progress);

  const float attack_rms = windowRms(mono, 0U, 480U);
  const float body_rms = windowRms(mono, 2400U, 2400U);
  std::printf("attack_rms=%.5f body_rms=%.5f\n", attack_rms, body_rms);

  if (body_rms < 0.015f)
    return 2;
  if (interval < 3.f)
    return 15;
  if (!saw_slide_flag)
    return 12;
  if (max_progress < 0.35f)
    return 13;
  float moved = pitch_at_30ms - pitch_at_start;
  if (moved < 0.f)
    moved = -moved;
  if (moved < 1.f)
    return 14;

  return 0;
}
