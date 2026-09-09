#pragma once

/*
 * File: hclap.h
 *
 * Tempo-synced 808/909 hand clap for NTS-3.
 * Hold the pad to run a 16-step phrase. X is hit density, Y morphs the
 * circuit from TR-808 (analog noise, one VCA) to TR-909 (LFSR, dual VCA).
 *
 * Phrase / noise morph shared with HSnare via hpad_phrase.h. See RESEARCH.md.
 */

#include "fx_dsp.h"
#include "hpad_phrase.h"
#include "macros.h"
#include "utils/float_math.h"
#include <stdint.h>

class HClap : public hpad::PhraseProcessor<HClap>
{
public:
  friend class hpad::PhraseProcessor<HClap>;

  static constexpr uint32_t kVoiceCount = 6U;
  static constexpr float kVoiceGain = 0.38f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    DENS = 0U,
    TYPE,
    MIX,
    TONE,
    DEC,
    SNAP,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case DENS:
      dens_norm_ = param_10bit_to_f32(value);
      break;
    case TYPE:
      type_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case TONE:
      tone_norm_ = param_10bit_to_f32(value);
      break;
    case DEC:
      decay_norm_ = param_10bit_to_f32(value);
      break;
    case SNAP:
      snap_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *) override final
  {
    initPhrase(0x00C0FFEEu);
    tone_norm_ = 0.5f;
    decay_norm_ = 0.5f;
    snap_norm_ = 0.5f;
    trigger_count_ = 0U;
    resetVoices();
  }

  void reset() override final
  {
    resetPhrase();
    resetVoices();
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      if (!use_host_clock_)
        advanceInternalClockOneSample();
      advancePendingFlam();

      const float wet = renderVoices(noise_.next(type_norm_)) * mix_;
      out[0] = in[0] + wet;
      out[1] = in[1] + wet;
      in += 2;
      out += 2;
    }
  }

  void debugTrigger(float accent = 1.f) { onPhraseHit(accent); }

  uint32_t debugTriggerCount() const { return trigger_count_; }

private:
  struct Voice
  {
    bool active = false;
    float age = 0.f;
    float accent = 1.f;
    float crack_low = 0.f;
    float crack_band = 0.f;
    float room_low = 0.f;
    float room_band = 0.f;
  };

  static float burstEnv(float age, float spacing)
  {
    const float last_span = spacing * 2.f;
    const float first_span = spacing * 3.f;
    if (age < 0.f)
      return 0.f;
    if (age < first_span)
    {
      const float phase = age / spacing;
      const float within = phase - static_cast<float>(static_cast<int32_t>(phase));
      return 1.f - within;
    }
    const float last_age = age - first_span;
    if (last_age < last_span)
      return 1.f - last_age / last_span;
    return 0.f;
  }

  static float svfF(float hz)
  {
    const float clamped = fx::clip(hz, 120.f, 8000.f);
    return 2.f * fastersinfullf(3.14159265f * clamped / 48000.f);
  }

  static float bandpass(float input, float f, float damp, float &low, float &band)
  {
    low += f * band;
    const float high = input - low - damp * band;
    band += f * high;
    return band;
  }

  void resetVoices()
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
      voices_[voiceIndex] = Voice{};
    next_voice_index_ = 0U;
  }

  void onPhraseHit(float accent)
  {
    Voice &voice = voices_[next_voice_index_];
    next_voice_index_ = (next_voice_index_ + 1U) % kVoiceCount;
    voice.active = true;
    voice.age = 0.f;
    voice.accent = accent;
    ++trigger_count_;
  }

  float renderVoice(Voice &voice, float noise, float crack_f, float crack_damp, float room_f, float room_damp,
                    float spacing, float tail_tau, float tail_mix, float type_norm)
  {
    const float burst = burstEnv(voice.age, spacing) * voice.accent;
    const float tail = fasterexpf(-voice.age / tail_tau);
    const float crack = bandpass(noise, crack_f, crack_damp, voice.crack_low, voice.crack_band);
    const float room = bandpass(noise, room_f, room_damp, voice.room_low, voice.room_band);

    const float eight_oh_eight = crack * (burst + tail * tail_mix);
    const float nine_oh_nine = crack * burst + room * tail * tail_mix;
    const float sample = eight_oh_eight + type_norm * (nine_oh_nine - eight_oh_eight);

    voice.age += 1.f / getSampleRate();
    if (voice.age > spacing * 5.f + tail_tau * 6.f)
      voice.active = false;

    return sample * kVoiceGain;
  }

  float renderVoices(float noise)
  {
    const float type_norm = type_norm_;
    const float tone = tone_norm_ * 2.f - 1.f;
    const float tone_ratio = fasterpow2f(tone * 0.55f);
    const float snap = snap_norm_;
    const float decay = 0.45f + decay_norm_ * 1.7f;

    const float crack_hz = (1000.f + type_norm * 400.f) * tone_ratio;
    const float room_hz = (1000.f - type_norm * 150.f) * (0.92f + tone_norm_ * 0.16f);
    const float crack_q = 1.15f + type_norm * 0.55f;
    const float room_q = 1.15f - type_norm * 0.4f;
    const float spacing = 0.010f + type_norm * 0.002f;
    const float tail_tau = (0.100f + type_norm * 0.030f) * decay;
    const float tail_mix = (0.30f + type_norm * 0.12f) * (1.15f - snap * 0.7f);

    const float crack_f = svfF(crack_hz);
    const float room_f = svfF(room_hz);
    const float crack_damp = 1.f / crack_q;
    const float room_damp = 1.f / room_q;

    float sum = 0.f;
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
    {
      Voice &voice = voices_[voiceIndex];
      if (!voice.active)
        continue;
      sum += renderVoice(voice, noise, crack_f, crack_damp, room_f, room_damp, spacing, tail_tau, tail_mix,
                         type_norm);
    }

    return fx::softclip(dcBlockSum(sum));
  }

  Voice voices_[kVoiceCount];
  uint32_t next_voice_index_ = 0U;
  uint32_t trigger_count_ = 0U;
  float tone_norm_ = 0.5f;
  float decay_norm_ = 0.5f;
  float snap_norm_ = 0.5f;
};
