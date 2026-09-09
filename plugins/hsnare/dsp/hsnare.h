#pragma once

/*
 * File: hsnare.h
 *
 * Tempo-synced 808/909 snare for NTS-3.
 * Hold the pad to run a 16-step phrase. X is hit density, Y morphs the
 * circuit from TR-808 (bridged-T sines + one HPF snappy) to TR-909
 * (triangle VCOs, 20 ms pitch bend, split LPF/HPF snappy).
 *
 * Phrase / noise morph shared with HClap via hpad_phrase.h. See RESEARCH.md.
 */

#include "fx_dsp.h"
#include "hpad_phrase.h"
#include "macros.h"
#include "utils/float_math.h"
#include <stdint.h>

class HSnare : public hpad::PhraseProcessor<HSnare>
{
public:
  friend class hpad::PhraseProcessor<HSnare>;

  static constexpr uint32_t kVoiceCount = 6U;
  static constexpr float kVoiceGain = 0.42f;
  static constexpr float k808LowHz = 173.3f;
  static constexpr float k808HighHz = 336.0f;
  static constexpr float k909LowHz = 190.0f;
  static constexpr float k909HighHz = 332.0f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    DENS = 0U,
    TYPE,
    MIX,
    TONE,
    SNAP,
    TUNE,
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
    case SNAP:
      snap_norm_ = param_10bit_to_f32(value);
      break;
    case TUNE:
      tune_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *) override final
  {
    initPhrase(0xA5A5A5A5u);
    tone_norm_ = 0.42f;
    snap_norm_ = 0.55f;
    tune_norm_ = 0.5f;
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
    float low_phase = 0.f;
    float high_phase = 0.f;
    float low_env = 0.f;
    float high_env = 0.f;
    float snap_env = 0.f;
    float snap_lp_env = 0.f;
    float snap_hp_z = 0.f;
    float snap_lp_z = 0.f;
  };

  static float triangle(float phase)
  {
    const float folded = 4.f * phase;
    if (folded < 2.f)
      return folded - 1.f;
    return 3.f - folded;
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
    voice.low_phase = 0.f;
    voice.high_phase = 0.f;
    voice.low_env = 1.f;
    voice.high_env = 1.f;
    voice.snap_env = 1.f;
    voice.snap_lp_env = 1.f;
    ++trigger_count_;
  }

  float renderVoice(Voice &voice, float noise, float low_hz, float high_hz, float type_norm, float tone,
                    float snap, float low_decay, float high_decay, float snap_decay, float snap_lp_decay,
                    float hp_coeff, float lp_coeff)
  {
    const float bend = type_norm * 0.55f * fasterexpf(-voice.age / 0.012f);
    const float low_inc = (low_hz * (1.f + bend)) / getSampleRate();
    const float high_inc = (high_hz * (1.f + bend)) / getSampleRate();

    const float sine_low = fastersinfullf(voice.low_phase * 6.283185307179586f);
    const float sine_high = fastersinfullf(voice.high_phase * 6.283185307179586f);
    const float tri_low = fastertanhf(triangle(voice.low_phase) * 1.35f);
    const float tri_high = fastertanhf(triangle(voice.high_phase) * 1.35f);

    const float low_wave = sine_low + type_norm * (tri_low - sine_low);
    const float high_wave = sine_high + type_norm * (tri_high - sine_high);
    const float shell = ((1.f - tone) * low_wave * voice.low_env + tone * high_wave * voice.high_env) *
                        (0.95f + 0.2f * snap);

    voice.snap_hp_z += hp_coeff * (noise - voice.snap_hp_z);
    const float hp_noise = noise - voice.snap_hp_z;
    voice.snap_lp_z += lp_coeff * (noise - voice.snap_lp_z);

    const float snap_808 = hp_noise * voice.snap_env;
    const float snap_909 = voice.snap_lp_z * voice.snap_lp_env * 0.55f + hp_noise * voice.snap_env;
    const float snappy = (snap_808 + type_norm * (snap_909 - snap_808)) * snap * 1.15f;

    voice.low_phase = fx::wrap01(voice.low_phase + low_inc);
    voice.high_phase = fx::wrap01(voice.high_phase + high_inc);
    voice.low_env *= low_decay;
    voice.high_env *= high_decay;
    voice.snap_env *= snap_decay;
    voice.snap_lp_env *= snap_lp_decay;
    voice.age += 1.f / getSampleRate();

    if (voice.low_env < 0.0008f && voice.high_env < 0.0008f && voice.snap_env < 0.0008f &&
        voice.snap_lp_env < 0.0008f)
      voice.active = false;

    const float type_gain = 1.f - type_norm * 0.48f;
    return (shell * voice.accent + snappy) * kVoiceGain * type_gain;
  }

  float renderVoices(float noise)
  {
    const float type_norm = type_norm_;
    const float tone = fx::clip01(tone_norm_);
    const float snap = fx::clip01(snap_norm_);
    const float tune = fasterpow2f((tune_norm_ * 2.f - 1.f) * 0.7f);

    const float low_hz = (k808LowHz + type_norm * (k909LowHz - k808LowHz)) * tune;
    const float high_hz = (k808HighHz + type_norm * (k909HighHz - k808HighHz)) * tune;

    const float sample_rate = getSampleRate();
    const float low_decay = hpad::envCoeffNearOne(0.085f + type_norm * 0.07f, sample_rate);
    const float high_decay = hpad::envCoeffNearOne(0.070f + type_norm * 0.045f, sample_rate);
    const float snap_decay = hpad::envCoeffNearOne(0.075f - type_norm * 0.02f, sample_rate);
    const float snap_lp_decay = hpad::envCoeffNearOne(0.16f + type_norm * 0.04f, sample_rate);
    const float hp_coeff = fx::onePoleCoeff(1600.f + tone * 900.f + type_norm * 400.f, getSampleRate());
    const float lp_coeff = fx::onePoleCoeff(2800.f - type_norm * 400.f, getSampleRate());

    float sum = 0.f;
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
    {
      Voice &voice = voices_[voiceIndex];
      if (!voice.active)
        continue;
      sum += renderVoice(voice, noise, low_hz, high_hz, type_norm, tone, snap, low_decay, high_decay,
                         snap_decay, snap_lp_decay, hp_coeff, lp_coeff);
    }

    return fx::softclip(dcBlockSum(sum));
  }

  Voice voices_[kVoiceCount];
  uint32_t next_voice_index_ = 0U;
  uint32_t trigger_count_ = 0U;
  float tone_norm_ = 0.42f;
  float snap_norm_ = 0.55f;
  float tune_norm_ = 0.5f;
};
