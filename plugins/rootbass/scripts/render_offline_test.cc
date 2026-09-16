#include "rootbass.h"
#include "macros.h"
#include "runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

static void renderWithInput(RootBass &bass, const std::vector<float> &mono_in, std::vector<float> &mono_out)
{
  const uint32_t frames = static_cast<uint32_t>(mono_in.size());
  std::vector<float> in(frames * 2U, 0.f);
  std::vector<float> out(frames * 2U, 0.f);
  for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
  {
    in[sampleIndex * 2U] = mono_in[sampleIndex];
    in[sampleIndex * 2U + 1U] = mono_in[sampleIndex];
  }
  bass.process(in.data(), out.data(), frames);
  mono_out.clear();
  mono_out.reserve(frames);
  for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    mono_out.push_back(out[sampleIndex * 2U]);
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

static float energyAfter(const std::vector<float> &mono, uint32_t start, uint32_t length)
{
  double energy = 0.0;
  const uint32_t end = std::min(static_cast<uint32_t>(mono.size()), start + length);
  for (uint32_t sampleIndex = start; sampleIndex < end; ++sampleIndex)
  {
    const double sample = static_cast<double>(mono[sampleIndex]);
    energy += sample * sample;
  }
  return static_cast<float>(energy);
}

static std::vector<float> makeSine(float hz, uint32_t frames, float amplitude)
{
  std::vector<float> mono(frames, 0.f);
  const float increment = hz / 48000.f;
  float phase = 0.f;
  for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
  {
    mono[sampleIndex] = std::sin(phase * 6.283185307179586) * amplitude;
    phase += increment;
    if (phase >= 1.f)
      phase -= 1.f;
  }
  return mono;
}

static void setup(RootBass &bass)
{
  bass.init(nullptr);
  bass.setParameter(RootBass::OCT, 1); // -1 octave
  bass.setParameter(RootBass::WAVE, RootBass::WAVE_SAW);
  bass.setParameter(RootBass::MIX, 1000);
  bass.setParameter(RootBass::RHY, RootBass::RHY_TRESI);
  bass.setParameter(RootBass::DEC, 500);
  bass.setTempo(96.f);
}

int main()
{
  RootBass bass;
  setup(bass);

  // Feed ~A2 (110 Hz) long enough to fill the detector, then force analyze.
  const std::vector<float> tone = makeSine(110.f, 48000U, 0.35f);
  std::vector<float> dry_out;
  bass.touchEvent(0, k_unit_touch_phase_began, 512, 512);
  renderWithInput(bass, tone, dry_out);

  if (!bass.debugHavePitch())
  {
    std::printf("FAIL: no pitch lock on 110 Hz input\n");
    return 1;
  }

  const float midi = bass.debugDetectedMidi();
  // A2 = 45. Allow ±1 semitone for AMDF quantization.
  if (midi < 44.f || midi > 46.f)
  {
    std::printf("FAIL: expected ~A2 (45), got midi=%f\n", midi);
    return 1;
  }

  // Tresillo should fire 6 hits per bar.
  bass.debugResetCounters();
  for (uint32_t stepIndex = 0; stepIndex < 16U; ++stepIndex)
    bass.debugTriggerStep(stepIndex);
  if (bass.debugTriggers() != 6U)
  {
    std::printf("FAIL: tresillo triggers=%u want 6\n", bass.debugTriggers());
    return 1;
  }

  // Cinquillo denser than tresillo.
  bass.setParameter(RootBass::RHY, RootBass::RHY_CINQ);
  bass.debugResetCounters();
  for (uint32_t stepIndex = 0; stepIndex < 16U; ++stepIndex)
    bass.debugTriggerStep(stepIndex);
  if (bass.debugTriggers() != 10U)
  {
    std::printf("FAIL: cinquillo triggers=%u want 10\n", bass.debugTriggers());
    return 1;
  }

  // Single-hit body should last past a click (~20 ms).
  setup(bass);
  bass.debugForcePitch(36.f);
  bass.setParameter(RootBass::RHY, RootBass::RHY_TRESI);
  bass.setParameter(RootBass::DEC, 700);
  bass.touchEvent(0, k_unit_touch_phase_began, 512, 512);
  bass.debugTriggerStep(0U);
  std::vector<float> silence(4800U, 0.f);
  std::vector<float> hit_out;
  renderWithInput(bass, silence, hit_out);
  const float peak = peakAbs(hit_out);
  const float late = energyAfter(hit_out, 960U, 960U); // ~20–40 ms
  if (bass.debugTriggers() < 1U || peak < 0.02f || late < 1e-5f)
  {
    std::printf("FAIL: bass too short/weak peak=%f late=%f trig=%u\n", peak, late, bass.debugTriggers());
    return 1;
  }

  // Octave string mapping sanity via getParameterStrValue.
  if (std::string(bass.getParameterStrValue(RootBass::WAVE, 2)) != "SAW")
  {
    std::printf("FAIL: WAVE string\n");
    return 1;
  }
  if (std::string(bass.getParameterStrValue(RootBass::RHY, 1)) != "TRESI")
  {
    std::printf("FAIL: RHY string\n");
    return 1;
  }

  std::printf("ok midi=%.0f tresi=6 cinq=10 peak=%f late=%f\n", midi, peak, late);
  return 0;
}
