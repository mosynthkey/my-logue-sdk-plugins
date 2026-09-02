/*
 * File: render_wav.cc
 *
 * Renders the AirHorn engine offline into 48 kHz mono WAV files so the attack,
 * sustain and release stages can be auditioned without hardware.
 *
 * Build and run:
 *   g++ -O2 -I plugins/airhorn/dsp -o /tmp/airhorn_render_wav \
 *       plugins/airhorn/scripts/render_wav.cc
 *   /tmp/airhorn_render_wav <out-dir>
 */

#include "airhorn_engine.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

constexpr float kSampleRate = AirHornEngine::kHostSampleRate;
constexpr int32_t kFadeCentre = 511; // matches the engine default of tau ~1.8 s
constexpr int32_t kFadeHold = 0;     // 0 disables the sustain fade entirely

uint32_t toSamples(float seconds)
{
  return static_cast<uint32_t>(seconds * kSampleRate + 0.5f);
}

// Excerpts that do not start at note-on need edges tapered, otherwise the cut
// is heard as a click that is not part of the engine output.
void applyEdgeFades(std::vector<float> &samples, float fade_in_ms, float fade_out_ms)
{
  const uint32_t fade_in = toSamples(fade_in_ms * 0.001f);
  const uint32_t fade_out = toSamples(fade_out_ms * 0.001f);
  const uint32_t count = static_cast<uint32_t>(samples.size());

  for (uint32_t sampleIndex = 0; sampleIndex < fade_in && sampleIndex < count; ++sampleIndex)
    samples[sampleIndex] *= static_cast<float>(sampleIndex) / static_cast<float>(fade_in);

  for (uint32_t sampleIndex = 0; sampleIndex < fade_out && sampleIndex < count; ++sampleIndex)
    samples[count - 1U - sampleIndex] *= static_cast<float>(sampleIndex) / static_cast<float>(fade_out);
}

void pushLittleEndian(std::vector<uint8_t> &bytes, uint32_t value, uint32_t width)
{
  for (uint32_t byteIndex = 0; byteIndex < width; ++byteIndex)
    bytes.push_back(static_cast<uint8_t>((value >> (8U * byteIndex)) & 0xFFU));
}

bool writeWav(const std::string &path, const std::vector<float> &samples)
{
  const uint32_t data_bytes = static_cast<uint32_t>(samples.size()) * 2U;
  std::vector<uint8_t> header;
  const char *riff = "RIFF";
  header.insert(header.end(), riff, riff + 4);
  pushLittleEndian(header, 36U + data_bytes, 4U);
  const char *wave_fmt = "WAVEfmt ";
  header.insert(header.end(), wave_fmt, wave_fmt + 8);
  pushLittleEndian(header, 16U, 4U);                                    // fmt chunk size
  pushLittleEndian(header, 1U, 2U);                                     // PCM
  pushLittleEndian(header, 1U, 2U);                                     // mono
  pushLittleEndian(header, static_cast<uint32_t>(kSampleRate), 4U);
  pushLittleEndian(header, static_cast<uint32_t>(kSampleRate) * 2U, 4U); // byte rate
  pushLittleEndian(header, 2U, 2U);                                     // block align
  pushLittleEndian(header, 16U, 2U);                                    // bits per sample
  const char *data = "data";
  header.insert(header.end(), data, data + 4);
  pushLittleEndian(header, data_bytes, 4U);

  std::vector<uint8_t> pcm;
  pcm.reserve(data_bytes);
  for (const float sample : samples)
  {
    float clamped = sample;
    if (clamped > 1.f)
      clamped = 1.f;
    if (clamped < -1.f)
      clamped = -1.f;
    const int32_t quantised = static_cast<int32_t>(std::lround(clamped * 32767.f));
    pushLittleEndian(pcm, static_cast<uint32_t>(static_cast<int16_t>(quantised)), 2U);
  }

  FILE *handle = std::fopen(path.c_str(), "wb");
  if (handle == nullptr)
    return false;
  std::fwrite(header.data(), 1U, header.size(), handle);
  std::fwrite(pcm.data(), 1U, pcm.size(), handle);
  std::fclose(handle);
  return true;
}

struct Section
{
  const char *file_name;
  const char *label;
  int32_t fade_param;
  float skip_seconds;
  float gate_seconds;
  float tail_seconds;
};

std::vector<float> renderSection(const Section &section)
{
  AirHornEngine engine;
  engine.init();
  engine.setParameter(AirHornEngine::LEVEL, 1023);
  engine.setParameter(AirHornEngine::MIX, 1000);
  engine.setParameter(AirHornEngine::FADE, section.fade_param);
  engine.startVoice(127, 60);

  const float output_gain = engine.outputLevel();
  const uint32_t skip_count = toSamples(section.skip_seconds);
  const uint32_t gate_count = toSamples(section.gate_seconds);
  const uint32_t tail_count = toSamples(section.tail_seconds);

  std::vector<float> samples;
  samples.reserve(gate_count + tail_count);
  for (uint32_t sampleIndex = 0; sampleIndex < gate_count; ++sampleIndex)
  {
    const float sample = engine.renderMono() * output_gain;
    if (sampleIndex >= skip_count)
      samples.push_back(sample);
  }

  engine.releaseAll();
  for (uint32_t sampleIndex = 0; sampleIndex < tail_count; ++sampleIndex)
    samples.push_back(engine.renderMono() * output_gain);

  applyEdgeFades(samples, section.skip_seconds > 0.f ? 5.f : 0.f, 5.f);
  return samples;
}

} // namespace

int main(int argc, char **argv)
{
  const std::string out_dir = (argc > 1) ? argv[1] : ".";

  const Section sections[] = {
      {"airhorn_attack.wav", "pitch drop from +6.4 semitones down to the settled tone",
       kFadeCentre, 0.f, 0.6f, 0.f},
      {"airhorn_sustain.wav", "settled loop, FADE=0 flat hold, ~7 loop passes",
       kFadeHold, 0.8f, 3.8f, 0.f},
      {"airhorn_release.wav", "sustain into the note-off release tail",
       kFadeHold, 0.8f, 1.8f, 1.6f},
      {"airhorn_full_note.wav", "note-on to note-off with the default FADE of ~1.8 s",
       kFadeCentre, 0.f, 6.f, 1.f},
  };

  for (const Section &section : sections)
  {
    const std::vector<float> samples = renderSection(section);
    const std::string path = out_dir + "/" + section.file_name;
    if (!writeWav(path, samples))
    {
      std::fprintf(stderr, "failed to write %s\n", path.c_str());
      return 1;
    }

    float peak = 0.f;
    double sum_squares = 0.0;
    for (const float sample : samples)
    {
      const float magnitude = std::fabs(sample);
      if (magnitude > peak)
        peak = magnitude;
      sum_squares += static_cast<double>(sample) * static_cast<double>(sample);
    }
    const double rms = std::sqrt(sum_squares / static_cast<double>(samples.size()));
    std::printf("%-24s %6.2f s peak=%.3f rms=%.3f  %s\n",
                section.file_name,
                samples.size() / static_cast<double>(kSampleRate),
                peak, rms, section.label);
  }

  return 0;
}
