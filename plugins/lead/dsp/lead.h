#pragma once

/*
 * File: lead.h
 *
 * SuperSaw lead for NTS-3. X selects a scale degree across ~5 octaves with
 * portamento; Y sets vibrato depth. Depth sets glide time. Unison / Detune /
 * Spread / Scale / Key are edit knobs.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class Lead : public Processor
{
public:
  static constexpr uint32_t kMaxVoices = 9U;
  static constexpr float kPitchSpanOctaves = 5.f;
  static constexpr float kPortaMinSeconds = 0.002f;
  static constexpr float kPortaMaxSeconds = 1.6f;
  static constexpr float kVibratoHz = 5.6f;
  static constexpr float kVibratoMaxSemis = 0.95f;
  static constexpr float kDetuneMaxCents = 42.f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    PITCH = 0U,
    VIBR,
    PORTA,
    SCALE,
    KEY,
    UNI,
    DETUN,
    SPRD,
    NUM_PARAMS
  };

  enum ScaleId : uint8_t
  {
    SCALE_IONIAN = 0U,
    SCALE_DORIAN,
    SCALE_PHRYGIAN,
    SCALE_LYDIAN,
    SCALE_MIXOLYDIAN,
    SCALE_AEOLIAN,
    SCALE_LOCRIAN,
    SCALE_MAJPENT,
    SCALE_MINPENT,
    SCALE_CHROMATIC,
    SCALE_COUNT
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case PITCH:
      pitch_norm_ = param_10bit_to_f32(value);
      break;
    case VIBR:
      vibr_norm_ = param_10bit_to_f32(value);
      break;
    case PORTA:
    {
      const float norm = param_10bit_to_f32(value);
      // Squared map keeps short glides editable, long glides available at the top.
      porta_seconds_ = kPortaMinSeconds + norm * norm * (kPortaMaxSeconds - kPortaMinSeconds);
      break;
    }
    case SCALE:
      scale_id_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, static_cast<float>(SCALE_COUNT - 1)));
      break;
    case KEY:
      key_note_ = static_cast<int8_t>(fx::clip(static_cast<float>(value), 24.f, 60.f));
      break;
    case UNI:
      unison_sel_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, 4.f));
      break;
    case DETUN:
      detune_norm_ = param_10bit_to_f32(value);
      break;
    case SPRD:
      spread_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    static const char *scale_names[SCALE_COUNT] = {
        "IONIAN", "DORIAN", "PHRYG", "LYDIAN", "MIXOLY", "AEOLIA", "LOCRIA", "MAJPNT", "MINPNT", "CHROM"};
    static const char *uni_names[5] = {"1", "3", "5", "7", "9"};
    static char key_label[8];

    if (index == SCALE && value >= 0 && value < static_cast<int32_t>(SCALE_COUNT))
      return scale_names[value];
    if (index == UNI && value >= 0 && value <= 4)
      return uni_names[value];
    if (index == KEY)
    {
      static const char *kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
      int32_t note = value;
      if (note < 0)
        note = 0;
      if (note > 127)
        note = 127;
      char *out = key_label;
      const char *name = kNames[note % 12];
      while (*name)
        *out++ = *name++;
      *out++ = static_cast<char>('0' + (note / 12) - 1);
      *out = '\0';
      return key_label;
    }
    return nullptr;
  }

  void init(float *) override final
  {
    amp_ = 0.f;
    porta_note_ = static_cast<float>(key_note_);
    vib_phase_ = 0.f;
    pad_held_ = false;
    rng_ = 0xC0FFEEu;
    for (uint32_t voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
    {
      phase_[voiceIndex] = fx::randomFloat(rng_);
      pan_left_[voiceIndex] = 0.5f;
      pan_right_[voiceIndex] = 0.5f;
    }
  }

  void reset() override final
  {
    amp_ = 0.f;
    vib_phase_ = 0.f;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    const bool held = phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
                      phase == k_unit_touch_phase_stationary;
    if (held && !pad_held_)
    {
      for (uint32_t voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
        phase_[voiceIndex] = fx::randomFloat(rng_);
      // Jump to the fingered note on fresh touch so the first attack is in key.
      porta_note_ = quantizedTargetNote();
    }
    pad_held_ = held;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    (void)raw;
    const float sample_rate = getSampleRate();
    const float porta_coeff = portaCoeff(porta_seconds_, sample_rate);
    const float amp_atk = ampCoeff(0.012f, sample_rate);
    const float amp_rel = ampCoeff(0.09f, sample_rate);
    const float vib_inc = kVibratoHz / sample_rate;
    const uint32_t voice_count = unisonVoiceCount();
    const float voice_gain = voiceNormGain(voice_count);
    updatePanTable(voice_count);

    static const float kDetuneShape[kMaxVoices] = {
        0.f, -0.14f, 0.14f, -0.42f, 0.42f, -0.73f, 0.73f, -1.f, 1.f};

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      if (pad_held_)
        porta_note_ += (quantizedTargetNote() - porta_note_) * porta_coeff;

      vib_phase_ = fx::wrap01(vib_phase_ + vib_inc);
      // Triangle LFO keeps vibrato aggressive without harsh FM sidebands.
      const float tri = (vib_phase_ < 0.5f) ? (vib_phase_ * 4.f - 1.f) : (3.f - vib_phase_ * 4.f);
      const float vibrato_semis = tri * vibr_norm_ * kVibratoMaxSemis;
      const float base_note = porta_note_ + vibrato_semis;

      float left = 0.f;
      float right = 0.f;
      for (uint32_t voiceIndex = 0; voiceIndex < voice_count; ++voiceIndex)
      {
        const float cents = kDetuneShape[voiceIndex] * detune_norm_ * kDetuneMaxCents;
        const float note = base_note + cents * (1.f / 100.f);
        const float inc = fx::noteToInc(note, sample_rate);
        phase_[voiceIndex] = fx::wrap01(phase_[voiceIndex] + inc);
        const float saw = fx::blepSaw(phase_[voiceIndex], inc);
        left += saw * pan_left_[voiceIndex];
        right += saw * pan_right_[voiceIndex];
      }

      const float amp_coeff = pad_held_ ? amp_atk : amp_rel;
      amp_ += ((pad_held_ ? 1.f : 0.f) - amp_) * amp_coeff;

      // Self-contained synth: always fully wet (Depth is portamento time).
      const float wet_left = fx::softclip(left * voice_gain * amp_ * 1.05f);
      const float wet_right = fx::softclip(right * voice_gain * amp_ * 1.05f);
      out[0] = wet_left;
      out[1] = wet_right;
      in += 2;
      out += 2;
    }
  }

private:
  static float ampCoeff(float seconds, float sample_rate)
  {
    const float clamped = fx::clip(seconds, 0.004f, 0.5f);
    const float x = -1.f / (clamped * sample_rate);
    // One-pole lerp alpha ≈ 1 - e^x ≈ -x for small |x|.
    return fx::clip(-x, 0.f, 1.f);
  }

  static float portaCoeff(float seconds, float sample_rate)
  {
    const float clamped = fx::clip(seconds, kPortaMinSeconds, kPortaMaxSeconds);
    const float x = -1.f / (clamped * sample_rate);
    return fx::clip(-x, 0.f, 1.f);
  }

  static uint32_t scaleLength(uint8_t scale_id)
  {
    if (scale_id == SCALE_CHROMATIC)
      return 12U;
    if (scale_id == SCALE_MAJPENT || scale_id == SCALE_MINPENT)
      return 5U;
    return 7U;
  }

  static const int8_t *scaleIntervals(uint8_t scale_id)
  {
    static const int8_t kIonian[] = {0, 2, 4, 5, 7, 9, 11};
    static const int8_t kDorian[] = {0, 2, 3, 5, 7, 9, 10};
    static const int8_t kPhrygian[] = {0, 1, 3, 5, 7, 8, 10};
    static const int8_t kLydian[] = {0, 2, 4, 6, 7, 9, 11};
    static const int8_t kMixolydian[] = {0, 2, 4, 5, 7, 9, 10};
    static const int8_t kAeolian[] = {0, 2, 3, 5, 7, 8, 10};
    static const int8_t kLocrian[] = {0, 1, 3, 5, 6, 8, 10};
    static const int8_t kMajPent[] = {0, 2, 4, 7, 9};
    static const int8_t kMinPent[] = {0, 3, 5, 7, 10};
    static const int8_t kChromatic[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

    switch (scale_id)
    {
    case SCALE_DORIAN:
      return kDorian;
    case SCALE_PHRYGIAN:
      return kPhrygian;
    case SCALE_LYDIAN:
      return kLydian;
    case SCALE_MIXOLYDIAN:
      return kMixolydian;
    case SCALE_AEOLIAN:
      return kAeolian;
    case SCALE_LOCRIAN:
      return kLocrian;
    case SCALE_MAJPENT:
      return kMajPent;
    case SCALE_MINPENT:
      return kMinPent;
    case SCALE_CHROMATIC:
      return kChromatic;
    case SCALE_IONIAN:
    default:
      return kIonian;
    }
  }

  float quantizedTargetNote() const
  {
    const uint32_t length = scaleLength(scale_id_);
    const int8_t *intervals = scaleIntervals(scale_id_);
    const uint32_t total_steps = static_cast<uint32_t>(kPitchSpanOctaves * static_cast<float>(length) + 0.5f);
    uint32_t step_index = static_cast<uint32_t>(pitch_norm_ * static_cast<float>(total_steps) + 0.5f);
    if (step_index > total_steps)
      step_index = total_steps;

    const uint32_t octave = step_index / length;
    const uint32_t degree = step_index % length;
    return static_cast<float>(key_note_) + static_cast<float>(octave * 12U) + static_cast<float>(intervals[degree]);
  }

  uint32_t unisonVoiceCount() const
  {
    static const uint32_t kCounts[5] = {1U, 3U, 5U, 7U, 9U};
    const uint8_t sel = (unison_sel_ > 4U) ? 4U : unison_sel_;
    return kCounts[sel];
  }

  static float voiceNormGain(uint32_t voice_count)
  {
    static const float kGain[10] = {
        0.f, 0.34f, 0.28f, 0.24f, 0.22f, 0.2f, 0.185f, 0.175f, 0.165f, 0.155f};
    if (voice_count > 9U)
      return kGain[9];
    return kGain[voice_count];
  }

  void updatePanTable(uint32_t voice_count)
  {
    static const float kPanOffset[kMaxVoices] = {
        0.f, -0.7f, 0.7f, -0.45f, 0.45f, -0.28f, 0.28f, -0.9f, 0.9f};
    for (uint32_t voiceIndex = 0; voiceIndex < voice_count; ++voiceIndex)
    {
      const float pan = fx::clip01(0.5f + spread_norm_ * kPanOffset[voiceIndex]);
      pan_left_[voiceIndex] = 1.f - pan;
      pan_right_[voiceIndex] = pan;
    }
  }

  float phase_[kMaxVoices] = {};
  float pan_left_[kMaxVoices] = {};
  float pan_right_[kMaxVoices] = {};
  float amp_ = 0.f;
  float porta_note_ = 36.f;
  float vib_phase_ = 0.f;
  float pitch_norm_ = 0.35f;
  float vibr_norm_ = 0.35f;
  float detune_norm_ = 0.55f;
  float spread_norm_ = 0.7f;
  float porta_seconds_ = 0.15f;
  uint32_t rng_ = 1U;
  int8_t key_note_ = 36;
  uint8_t scale_id_ = SCALE_IONIAN;
  uint8_t unison_sel_ = 2U; // 5 voices
  bool pad_held_ = false;
};
