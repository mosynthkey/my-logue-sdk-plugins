#include "kaocid.h"
#include "macros.h"
#include "runtime.h"

#include <cmath>
#include <cstdint>
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

static uint64_t phraseFingerprint(const Kaocid &synth)
{
  uint64_t fingerprint = 0U;
  for (uint32_t stepIndex = 0; stepIndex < Kaocid::kStepsPerBar; ++stepIndex)
  {
    fingerprint *= 37U;
    fingerprint += static_cast<uint64_t>(synth.debugDegree(stepIndex) + 2);
    fingerprint *= 3U;
    fingerprint += synth.debugSlide(stepIndex) ? 1U : 0U;
    fingerprint *= 3U;
    fingerprint += synth.debugAccent(stepIndex) ? 1U : 0U;
  }
  return fingerprint;
}

int main()
{
  Kaocid synth;
  uint64_t seen[48];
  uint32_t unique_count = 0U;
  uint32_t min_notes = 16U;
  uint32_t max_notes = 0U;
  uint32_t glide_phrases = 0U;
  uint32_t rest_on_first = 0U;
  uint32_t slide_seed_x = 0U;
  uint32_t slide_seed_y = 0U;
  bool found_slide = false;

  for (uint32_t seedIndex = 0; seedIndex < 48U; ++seedIndex)
  {
    setup(synth);
    const uint32_t touch_x = 40U + seedIndex * 73U;
    const uint32_t touch_y = 90U + seedIndex * 41U;
    synth.touchEvent(0, k_unit_touch_phase_began, touch_x, touch_y);

    const uint32_t note_count = synth.debugNoteCount();
    if (note_count == 0U)
    {
      std::printf("seed %u produced silence\n", seedIndex);
      return 10;
    }
    if (note_count < min_notes)
      min_notes = note_count;
    if (note_count > max_notes)
      max_notes = note_count;
    if (synth.debugDegree(0) < 0)
      ++rest_on_first;
    if (synth.debugGlideCount() > 0U)
      ++glide_phrases;

    const uint64_t fingerprint = phraseFingerprint(synth);
    bool is_unique = true;
    for (uint32_t seenIndex = 0; seenIndex < unique_count; ++seenIndex)
    {
      if (seen[seenIndex] == fingerprint)
      {
        is_unique = false;
        break;
      }
    }
    if (is_unique && unique_count < 48U)
      seen[unique_count++] = fingerprint;

    if (!found_slide)
    {
      for (uint32_t stepIndex = 0; stepIndex < Kaocid::kStepsPerBar; ++stepIndex)
      {
        const uint32_t next_index = (stepIndex + 1U) % Kaocid::kStepsPerBar;
        if (synth.debugSlide(stepIndex) && synth.debugDegree(stepIndex) >= 0 &&
            synth.debugDegree(next_index) >= 0)
        {
          found_slide = true;
          slide_seed_x = touch_x;
          slide_seed_y = touch_y;
          break;
        }
      }
    }
  }

  std::printf("unique=%u notes=%u..%u glides=%u rest0=%u\n", unique_count, min_notes, max_notes,
              glide_phrases, rest_on_first);

  if (unique_count < 40U)
    return 15;
  if (min_notes > 6U || max_notes < 12U)
    return 16;
  if (glide_phrases < 8U)
    return 17;
  if (rest_on_first == 0U)
    return 18;
  if (!found_slide)
    return 11;

  setup(synth);
  synth.touchEvent(0, k_unit_touch_phase_began, slide_seed_x, slide_seed_y);

  const uint32_t glide_count = synth.debugGlideCount();
  std::printf("glide_count=%u phrase=", glide_count);
  for (uint32_t stepIndex = 0; stepIndex < Kaocid::kStepsPerBar; ++stepIndex)
  {
    const int degree = static_cast<int>(synth.debugDegree(stepIndex));
    std::printf("%s%d%s", (stepIndex == 0U) ? "" : " ", degree, synth.debugSlide(stepIndex) ? "s" : "");
  }
  std::printf("\n");

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
    return 11;

  const uint32_t slide_dest = (slide_source + 1U) % Kaocid::kStepsPerBar;
  const float source_pitch = 36.f + static_cast<float>(synth.debugDegree(slide_source));
  const float dest_pitch = 36.f + static_cast<float>(synth.debugDegree(slide_dest));
  float interval = dest_pitch - source_pitch;
  if (interval < 0.f)
    interval = -interval;

  const uint32_t samples_per_step = 6000U;
  const uint32_t slide_start = (slide_source + 1U) * samples_per_step;
  std::vector<float> mono;
  mono.reserve(slide_start + 4000U);

  bool saw_slide_flag = false;
  float max_progress = 0.f;

  uint32_t sample_count = 0U;
  while (sample_count < slide_start + 3600U)
  {
    renderBlock(synth, 64U, mono);
    sample_count += 64U;
    if (sample_count >= slide_start && sample_count < slide_start + 64U)
      saw_slide_flag = synth.debugSlideActive();

    if (sample_count >= slide_start && interval > 0.1f)
    {
      float progress = (synth.debugPitch() - source_pitch) / (dest_pitch - source_pitch);
      if (progress < 0.f)
        progress = -progress;
      if (progress > max_progress)
        max_progress = progress;
    }
  }

  const float body_rms = windowRms(mono, 0U, static_cast<uint32_t>(mono.size()));
  std::printf("slide_active=%d body_rms=%.5f\n", saw_slide_flag ? 1 : 0, body_rms);

  if (body_rms < 0.008f)
    return 2;
  if (interval > 0.1f && (!saw_slide_flag || max_progress < 0.35f))
    return 14;

  return 0;
}
