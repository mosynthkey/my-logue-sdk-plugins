#pragma once

/*
 * File: kaocid.h
 *
 * TB-303 style monophonic acid bass with automatic 16-step phrase generator
 * for NTS-3 kaoss pad. Hold the pad to run the sequencer; each new touch
 * regenerates a random acid pattern (rhythm, scale, accent, and slide style
 * vary per touch). X = cutoff, Y = resonance, Depth = mix. ROOT sets the
 * phrase key.
 *
 * Panel knobs follow the TB-303: waveform, cutoff, resonance, env mod, decay,
 * accent. Accented steps use a fixed ~200 ms MEG decay, louder VCA, and a
 * resonance-dependent Accent Sweep ("wapp") into the filter. Distortion /
 * delay / reverb are left to other NTS-3 slots.
 *
 * Filter coefficients follow gsynth TB-303 (Andy Sloane, 2001), adapted for
 * 48 kHz: cutoff / resonance / env mod are the same 0..1 domain as gsynth.
 * NTS-3 genericfx cannot resolve libm, so pitch/env/filter use logue-sdk
 * float_math.h. VCA is after the VCF (analog 303 order) so the note body
 * remains after the envelope; gsynth fed the VCA into the filter which
 * collapsed to attack pings on device.
 */

#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class Kaocid : public Processor
{
public:
  static constexpr uint32_t kStepsPerBar = 16U;
  static constexpr uint32_t kFilterEnvRecalcInterval = 64U;
  static constexpr float kOutputGain = 0.45f;
  static constexpr uint32_t kPhraseStyleCount = 8U;
  static constexpr float kAccentDecaySec = 0.2f;
  static constexpr float kAccentVcaBoost = 0.9f;
  static constexpr float kAccentEnvFeed = 0.4f;
  static constexpr float kAccentSweepDepth = 0.55f;
  static constexpr float kAccentCapRetain = 0.55f;
  static constexpr float kAccentCapCharge = 0.85f;
  static constexpr float kAccentLagMinSec = 0.002f;
  static constexpr float kAccentLagMaxSec = 0.03f;
  static constexpr float kSlideTauSec = 100000.f * 0.00000022f;
  static constexpr float kSlideSettleSemitones = 0.01f;
  static constexpr float kVcaAttackSec = 0.004f;
  static constexpr float kVcaReleaseSec = 0.045f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    CUT = 0U,
    RES,
    MIX,
    WAVE,
    ENV,
    DEC,
    ACC,
    ROOT,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case CUT:
      cutoff_norm_ = param_10bit_to_f32(value);
      updateFilterTargets();
      break;
    case RES:
      resonance_norm_ = param_10bit_to_f32(value);
      updateFilterTargets();
      break;
    case MIX:
      mix_ = value / 1000.f;
      if (mix_ < 0.f)
        mix_ = 0.f;
      if (mix_ > 1.f)
        mix_ = 1.f;
      break;
    case WAVE:
      square_wave_ = value != 0;
      break;
    case ENV:
      env_mod_norm_ = param_10bit_to_f32(value);
      updateFilterTargets();
      break;
    case DEC:
      decay_norm_ = param_10bit_to_f32(value);
      updateFilterTargets();
      break;
    case ACC:
      accent_norm_ = param_10bit_to_f32(value);
      break;
    case ROOT:
    {
      int32_t midi_note = value;
      if (midi_note < 24)
        midi_note = 24;
      if (midi_note > 48)
        midi_note = 48;
      root_note_ = static_cast<int8_t>(midi_note);
      retuneCurrentNote();
      break;
    }
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index == WAVE)
      return (value != 0) ? "SQR" : "SAW";

    if (index != ROOT)
      return nullptr;

    static char note_label[8];
    static const char *kPitchClasses[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

    int32_t midi_note = value;
    if (midi_note < 0)
      midi_note = 0;
    if (midi_note > 127)
      midi_note = 127;

    const int32_t pitch_class = midi_note % 12;
    const int32_t octave = (midi_note / 12) - 1;
    char *out = note_label;
    const char *pitch_name = kPitchClasses[pitch_class];
    while (*pitch_name != '\0')
      *out++ = *pitch_name++;
    if (octave < 0)
    {
      *out++ = '-';
      *out++ = static_cast<char>('0' - octave);
    }
    else
    {
      *out++ = static_cast<char>('0' + octave);
    }
    *out = '\0';
    return note_label;
  }

  void init(float *) override final
  {
    cutoff_norm_ = 0.62f;
    resonance_norm_ = 0.55f;
    mix_ = 1.f;
    square_wave_ = false;
    env_mod_norm_ = 0.6f;
    decay_norm_ = 0.4f;
    accent_norm_ = 0.55f;
    root_note_ = 36;
    bpm_ = 120.f;
    const float sample_rate = getSampleRate();
    slide_coeff_ = 1.f - fasterexpf(-1.f / (kSlideTauSec * sample_rate));
    vca_attack_ = 1.f - fasterexpf(-1.f / (kVcaAttackSec * sample_rate));
    vca_decay_ = fasterexpf(-1.f / (kVcaReleaseSec * sample_rate));
    accent_release_coeff_ =
        fasterexpf(-2.3025851f * static_cast<float>(kFilterEnvRecalcInterval) / (kAccentDecaySec * sample_rate));
    samples_per_tick_ = sample_rate * 15.f / bpm_;
    running_ = false;
    phrase_seed_ = 1U;
    resetVoice();
    updateFilterTargets();
    generatePhrase(phrase_seed_);
  }

  void reset() override final
  {
    running_ = false;
    resetVoice();
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 20.f && tempo < 999.f)
    {
      bpm_ = tempo;
      samples_per_tick_ = getSampleRate() * 15.f / bpm_;
    }
  }

  void tempo4ppqnTick(uint32_t counter) override final
  {
    (void)counter;
  }

  void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    (void)id;

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      running_ = false;
      gate_off_requested_ = true;
      return;
    }

    if (phase != k_unit_touch_phase_began && phase != k_unit_touch_phase_moved &&
        phase != k_unit_touch_phase_stationary)
      return;

    // Firmware may re-send began/stationary while the finger is down.
    if (running_)
      return;

    phrase_seed_ = mixSeed(phrase_seed_, x, y);
    generatePhrase(phrase_seed_);
    step_index_ = 0U;
    samples_until_tick_ = samples_per_tick_;
    running_ = true;
    triggerStep(0U, false);
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    const float dry_gain = 1.f - mix_;
    const float wet_gain = mix_ * kOutputGain;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      if (running_)
        advanceClockOneSample();

      const float wet = renderSample() * wet_gain;
      out[0] = in[0] * dry_gain + wet;
      out[1] = in[1] * dry_gain + wet;
      in += 2;
      out += 2;
    }
  }

#ifdef KAOCID_OFFLINE_TEST
  float debugPitch() const { return vco_pitch_; }
  bool debugSlideActive() const { return slide_active_; }
  uint32_t debugGlideCount() const
  {
    uint32_t glide_count = 0U;
    for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
    {
      if (phrase_[stepIndex].slide)
        ++glide_count;
    }
    return glide_count;
  }
  int8_t debugDegree(uint32_t step_index) const { return phrase_[step_index].degree; }
  bool debugSlide(uint32_t step_index) const { return phrase_[step_index].slide; }
  bool debugAccent(uint32_t step_index) const { return phrase_[step_index].accent; }
  uint32_t debugNoteCount() const
  {
    uint32_t note_count = 0U;
    for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
    {
      if (phrase_[stepIndex].degree >= 0)
        ++note_count;
    }
    return note_count;
  }
#endif

private:
  struct Step
  {
    int8_t degree = -1;
    bool accent = false;
    bool slide = false;
  };

  static uint32_t mixSeed(uint32_t counter, uint32_t x, uint32_t y)
  {
    return counter * 2654435761U + x * 2246822519U + y * 3266489917U + 1U;
  }

  static uint32_t nextRandom(uint32_t &state)
  {
    state = state * 1664525U + 1013904223U;
    return state;
  }

  static uint32_t randomIndex(uint32_t &state, uint32_t count)
  {
    if (count <= 1U)
      return 0U;
    return (nextRandom(state) >> 8) % count;
  }

  static float randomFloat(uint32_t &state)
  {
    return static_cast<float>(nextRandom(state) >> 8) * (1.f / 16777216.f);
  }

  static bool euclideanGate(uint32_t stepIndex, uint32_t pulses, uint32_t steps)
  {
    return ((stepIndex * pulses) % steps) < pulses;
  }

  static float clipRange(float value, float min_value, float max_value)
  {
    if (value < min_value)
      return min_value;
    if (value > max_value)
      return max_value;
    return value;
  }

  static float noteToPhaseInc(float note)
  {
    const float semitones = note - 69.f;
    return (440.f / getSampleRate()) * fasterpow2f(semitones * (1.f / 12.f));
  }

  void resetVoice()
  {
    vco_phase_inc_ = 0.f;
    vco_pitch_ = 36.f;
    vco_pitch_target_ = 36.f;
    vco_phase_ = 0.f;
    vcf_cutoff_ = 0.f;
    vcf_env_mod_ = 0.58f;
    vcf_reso_ = 0.f;
    vcf_res_coeff_ = 0.f;
    vcf_env_decay_ = 0.f;
    vcf_env_pos_ = kFilterEnvRecalcInterval;
    vcf_a_ = 0.f;
    vcf_b_ = 0.f;
    vcf_c_ = 1.f;
    vcf_delay1_ = 0.f;
    vcf_delay2_ = 0.f;
    vcf_env_level_ = 0.f;
    vcf_env_end0_ = 0.f;
    vcf_env_span_ = 0.f;
    accent_sweep_ = 0.f;
    accent_sweep_target_ = 0.f;
    accent_cap_ = 0.f;
    accent_attack_coeff_ = 0.2f;
    vca_mode_ = 2;
    vca_level_ = 0.f;
    vca_target_ = 0.5f;
    gate_off_requested_ = false;
    slide_active_ = false;
    accent_active_ = false;
    current_degree_ = -1;
    step_index_ = 0U;
    samples_until_tick_ = samples_per_tick_;
    dc_prev_in_ = 0.f;
    dc_prev_out_ = 0.f;
  }

  void updateFilterTargets()
  {
    vcf_cutoff_ = clipRange(cutoff_norm_, 0.f, 1.f);
    vcf_reso_ = clipRange(resonance_norm_, 0.f, 1.f);
    vcf_env_mod_ = clipRange(env_mod_norm_, 0.f, 1.f);

    vcf_res_coeff_ = fasterexpf(-1.20f + 3.455f * vcf_reso_);
    recalcFilterEnvelope();
  }

  void recalcFilterEnvelope()
  {
    const float res_comp = 1.f - vcf_reso_;
    const float env_end1 = fasterexpf(6.109f + 1.5876f * vcf_env_mod_ + 2.1553f * vcf_cutoff_ - 1.2f * res_comp);
    const float env_end0 = fasterexpf(5.613f - 0.8f * vcf_env_mod_ + 2.1553f * vcf_cutoff_ - 0.7696f * res_comp);
    const float sample_rate_scale = 3.141592653589793f / getSampleRate();

    vcf_env_end0_ = env_end0 * sample_rate_scale;
    vcf_env_span_ = (env_end1 - env_end0) * sample_rate_scale;

    const float normal_decay_seconds = 0.2f + 2.3f * decay_norm_;
    const float interval = static_cast<float>(kFilterEnvRecalcInterval);
    vcf_env_decay_normal_ =
        fasterexpf(-2.3025851f * interval / (normal_decay_seconds * getSampleRate()));
    vcf_env_decay_accent_ =
        fasterexpf(-2.3025851f * interval / (kAccentDecaySec * getSampleRate()));
    vcf_env_decay_ = accent_active_ ? vcf_env_decay_accent_ : vcf_env_decay_normal_;

    const float lag_sec = kAccentLagMinSec + resonance_norm_ * (kAccentLagMaxSec - kAccentLagMinSec);
    accent_attack_coeff_ = 1.f - fasterexpf(-interval / (lag_sec * getSampleRate()));
    vcf_env_pos_ = kFilterEnvRecalcInterval;
  }

  void triggerAccentSweep()
  {
    // Resonance is the Accent Sweep lag pot: higher RES -> slower "wapp" rise.
    const float charge = accent_norm_ * kAccentCapCharge * (0.35f + 0.65f * resonance_norm_);
    accent_cap_ = clipRange(accent_cap_ * kAccentCapRetain + charge, 0.f, 1.5f);
    accent_sweep_target_ = accent_cap_;
    accent_sweep_ = 0.f;
    const float lag_sec = kAccentLagMinSec + resonance_norm_ * (kAccentLagMaxSec - kAccentLagMinSec);
    accent_attack_coeff_ =
        1.f - fasterexpf(-static_cast<float>(kFilterEnvRecalcInterval) / (lag_sec * getSampleRate()));
  }

  void clearAccentSweep()
  {
    accent_sweep_target_ = 0.f;
    accent_cap_ *= 0.85f;
  }

  void clearPhrase()
  {
    for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
    {
      phrase_[stepIndex].degree = -1;
      phrase_[stepIndex].accent = false;
      phrase_[stepIndex].slide = false;
    }
  }

  int8_t randomScaleDegree(uint32_t &rng, uint32_t scale_id) const
  {
    static const int8_t kMinorPent[] = {0, 3, 5, 7, 10, 12, 15, 17};
    static const int8_t kNaturalMinor[] = {0, 2, 3, 5, 7, 8, 10, 12, 15};
    static const int8_t kDorian[] = {0, 2, 3, 5, 7, 9, 10, 12, 14};
    static const int8_t kBlues[] = {0, 3, 5, 6, 7, 10, 12, 15};
    static const int8_t kPhrygian[] = {0, 1, 3, 5, 7, 8, 10, 12};
    static const int8_t kMajorPent[] = {0, 2, 4, 7, 9, 12, 14, 16};
    static const int8_t kFifthOctave[] = {0, 7, 12, 17};
    static const int8_t kChromaticAcid[] = {0, 1, 3, 5, 6, 7, 10, 12, 13};

    const int8_t *degrees = kMinorPent;
    uint32_t length = 8U;
    switch (scale_id % 8U)
    {
    case 1:
      degrees = kNaturalMinor;
      length = 9U;
      break;
    case 2:
      degrees = kDorian;
      length = 9U;
      break;
    case 3:
      degrees = kBlues;
      length = 8U;
      break;
    case 4:
      degrees = kPhrygian;
      length = 8U;
      break;
    case 5:
      degrees = kMajorPent;
      length = 8U;
      break;
    case 6:
      degrees = kFifthOctave;
      length = 4U;
      break;
    case 7:
      degrees = kChromaticAcid;
      length = 9U;
      break;
    default:
      break;
    }
    return degrees[randomIndex(rng, length)];
  }

  int8_t clampDegree(int32_t degree) const
  {
    while (degree < 0)
      degree += 12;
    while (degree > 17)
      degree -= 12;
    return static_cast<int8_t>(degree);
  }

  void ensureAnyNote(uint32_t &rng, uint32_t scale_id)
  {
    for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
    {
      if (phrase_[stepIndex].degree >= 0)
        return;
    }
    const uint32_t fallback_index = randomIndex(rng, kStepsPerBar);
    phrase_[fallback_index].degree = randomScaleDegree(rng, scale_id);
    phrase_[fallback_index].accent = true;
  }

  void placeEuclideanNotes(uint32_t &rng, uint32_t scale_id, uint32_t pulses)
  {
    for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
    {
      if (!euclideanGate(stepIndex, pulses, kStepsPerBar))
        continue;
      phrase_[stepIndex].degree = randomScaleDegree(rng, scale_id);
    }
  }

  void applyAccents(uint32_t &rng, uint32_t accent_style)
  {
    for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
    {
      if (phrase_[stepIndex].degree < 0)
      {
        phrase_[stepIndex].accent = false;
        continue;
      }

      bool accent = false;
      switch (accent_style % 5U)
      {
      case 0:
        accent = (stepIndex % 4U) == 0U || randomFloat(rng) > 0.78f;
        break;
      case 1:
        accent = (stepIndex % 4U) == 2U || randomFloat(rng) > 0.72f;
        break;
      case 2:
        accent = ((stepIndex & 1U) != 0U) && randomFloat(rng) > 0.35f;
        break;
      case 3:
        accent = randomFloat(rng) > 0.55f;
        break;
      default:
        accent = (stepIndex == 0U || stepIndex == 3U || stepIndex == 7U || stepIndex == 10U ||
                  stepIndex == 14U);
        break;
      }
      phrase_[stepIndex].accent = accent;
    }
  }

  void maybeWidenSlideInterval(uint32_t source_index, uint32_t dest_index, uint32_t &rng)
  {
    static const int8_t kGlideLeaps[] = {2, 3, 5, 7, 10, 12, -2, -3, -5, -7, -12};
    static constexpr uint32_t kLeapCount = sizeof(kGlideLeaps) / sizeof(kGlideLeaps[0]);

    const int8_t source_degree = phrase_[source_index].degree;
    int8_t dest_degree = phrase_[dest_index].degree;
    int32_t interval = dest_degree - source_degree;
    if (interval < 0)
      interval = -interval;

    if (interval >= 5 || randomFloat(rng) < 0.55f)
      return;

    const int8_t leap = kGlideLeaps[randomIndex(rng, kLeapCount)];
    dest_degree = clampDegree(source_degree + leap);
    phrase_[dest_index].degree = dest_degree;
  }

  void applySlides(uint32_t &rng, float probability, uint32_t min_glides, bool prefer_leaps)
  {
    uint32_t glide_count = 0U;
    for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
    {
      const uint32_t next_index = (stepIndex + 1U) % kStepsPerBar;
      if (phrase_[stepIndex].degree < 0 || phrase_[next_index].degree < 0)
        continue;
      if (randomFloat(rng) >= probability)
        continue;

      phrase_[stepIndex].slide = true;
      if (prefer_leaps)
        maybeWidenSlideInterval(stepIndex, next_index, rng);
      ++glide_count;
    }

    for (uint32_t searchIndex = 0; glide_count < min_glides && searchIndex < kStepsPerBar; ++searchIndex)
    {
      if (phrase_[searchIndex].slide || phrase_[searchIndex].degree < 0)
        continue;
      const uint32_t next_index = (searchIndex + 1U) % kStepsPerBar;
      if (phrase_[next_index].degree < 0)
        continue;
      phrase_[searchIndex].slide = true;
      ++glide_count;
    }
  }

  void copyHalfBar(uint32_t dest_offset, int32_t degree_shift)
  {
    for (uint32_t stepIndex = 0; stepIndex < 8U; ++stepIndex)
    {
      const Step &source = phrase_[stepIndex];
      Step &dest = phrase_[dest_offset + stepIndex];
      dest.accent = source.accent;
      dest.slide = source.slide;
      if (source.degree < 0)
        dest.degree = -1;
      else
        dest.degree = clampDegree(source.degree + degree_shift);
    }
  }

  void generatePhrase(uint32_t seed)
  {
    uint32_t rng = seed;
    clearPhrase();

    const uint32_t style = randomIndex(rng, kPhraseStyleCount);
    const uint32_t scale_id = randomIndex(rng, 8U);
    const uint32_t accent_style = randomIndex(rng, 5U);
    float slide_probability = 0.28f;
    uint32_t min_glides = 0U;
    bool prefer_leaps = randomFloat(rng) > 0.5f;

    switch (style)
    {
    case 0:
      placeEuclideanNotes(rng, scale_id, 3U + randomIndex(rng, 8U));
      slide_probability = 0.18f + randomFloat(rng) * 0.35f;
      min_glides = (randomFloat(rng) > 0.55f) ? 2U : 0U;
      break;
    case 1:
    {
      const uint32_t hit_count = 3U + randomIndex(rng, 4U);
      uint32_t placed = 0U;
      uint32_t guard = 0U;
      while (placed < hit_count && guard < 48U)
      {
        const uint32_t stepIndex = randomIndex(rng, kStepsPerBar);
        ++guard;
        if (phrase_[stepIndex].degree >= 0)
          continue;
        phrase_[stepIndex].degree = randomScaleDegree(rng, scale_id);
        ++placed;
      }
      slide_probability = 0.45f + randomFloat(rng) * 0.4f;
      min_glides = 1U;
      break;
    }
    case 2:
    {
      const uint32_t pulses = 6U + randomIndex(rng, 5U);
      for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
      {
        if (!euclideanGate(stepIndex, pulses, kStepsPerBar))
          continue;
        phrase_[stepIndex].degree = (randomFloat(rng) > 0.45f) ? 12 : 0;
      }
      slide_probability = 0.55f;
      min_glides = 2U;
      prefer_leaps = false;
      break;
    }
    case 3:
    {
      const uint32_t gallop_id = randomIndex(rng, 3U);
      for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
      {
        const uint32_t cell = stepIndex % 4U;
        bool hit = false;
        if (gallop_id == 0U)
          hit = cell != 3U;
        else if (gallop_id == 1U)
          hit = cell < 2U;
        else
          hit = cell != 1U;
        if (!hit)
          continue;
        phrase_[stepIndex].degree = randomScaleDegree(rng, scale_id);
      }
      slide_probability = 0.22f;
      break;
    }
    case 4:
      for (uint32_t stepIndex = 1; stepIndex < kStepsPerBar; stepIndex += 2U)
        phrase_[stepIndex].degree = randomScaleDegree(rng, scale_id);
      if (randomFloat(rng) > 0.4f)
        phrase_[0].degree = 0;
      slide_probability = 0.12f;
      break;
    case 5:
    {
      const uint32_t motif_hits = 3U + randomIndex(rng, 3U);
      uint32_t placed = 0U;
      uint32_t guard = 0U;
      while (placed < motif_hits && guard < 24U)
      {
        const uint32_t stepIndex = randomIndex(rng, 8U);
        ++guard;
        if (phrase_[stepIndex].degree >= 0)
          continue;
        phrase_[stepIndex].degree = randomScaleDegree(rng, scale_id);
        ++placed;
      }
      int32_t answer_shift = 0;
      const uint32_t answer_id = randomIndex(rng, 4U);
      if (answer_id == 1U)
        answer_shift = 12;
      else if (answer_id == 2U)
        answer_shift = -12;
      else if (answer_id == 3U)
        answer_shift = 3;
      copyHalfBar(8U, answer_shift);
      slide_probability = 0.3f;
      min_glides = 1U;
      break;
    }
    case 6:
    {
      const int8_t pitch_a = randomScaleDegree(rng, scale_id);
      int8_t pitch_b = randomScaleDegree(rng, scale_id);
      if (pitch_b == pitch_a)
        pitch_b = clampDegree(pitch_a + ((randomFloat(rng) > 0.5f) ? 7 : 12));
      for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
      {
        if ((stepIndex % 4U) == 3U && randomFloat(rng) > 0.35f)
          continue;
        phrase_[stepIndex].degree = ((stepIndex & 1U) == 0U) ? pitch_a : pitch_b;
      }
      slide_probability = 0.6f;
      min_glides = 3U;
      prefer_leaps = false;
      break;
    }
    default:
    {
      int32_t cursor = (randomFloat(rng) > 0.5f) ? 0 : 12;
      const int32_t walk = (cursor == 0) ? 1 : -1;
      for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
      {
        if ((stepIndex % 3U) == 2U && randomFloat(rng) > 0.4f)
          continue;
        phrase_[stepIndex].degree = clampDegree(cursor);
        cursor += walk * (2 + static_cast<int32_t>(randomIndex(rng, 3U)));
      }
      slide_probability = 0.38f;
      min_glides = 2U;
      prefer_leaps = true;
      break;
    }
    }

    ensureAnyNote(rng, scale_id);
    if (phrase_[0].degree >= 0 && randomFloat(rng) > 0.78f)
    {
      uint32_t other_notes = 0U;
      for (uint32_t stepIndex = 1; stepIndex < kStepsPerBar; ++stepIndex)
      {
        if (phrase_[stepIndex].degree >= 0)
          ++other_notes;
      }
      if (other_notes >= 2U)
      {
        phrase_[0].degree = -1;
        phrase_[0].accent = false;
        phrase_[0].slide = false;
      }
    }
    applyAccents(rng, accent_style);
    applySlides(rng, slide_probability, min_glides, prefer_leaps);
  }

  void triggerStep(uint32_t step_index, bool allow_slide_in)
  {
    const Step &step = phrase_[step_index];
    const uint32_t prev_index = (step_index + kStepsPerBar - 1U) % kStepsPerBar;
    const bool arriving_via_slide = allow_slide_in && phrase_[prev_index].slide &&
                                    phrase_[prev_index].degree >= 0;

    if (step.degree < 0)
    {
      if (!arriving_via_slide)
      {
        gate_off_requested_ = true;
        current_degree_ = -1;
        accent_active_ = false;
        clearAccentSweep();
      }
      return;
    }

    current_degree_ = step.degree;
    vco_pitch_target_ = static_cast<float>(root_note_ + step.degree);
    gate_off_requested_ = false;

    const bool use_accent = step.accent;
    accent_active_ = use_accent;
    vcf_env_decay_ = use_accent ? vcf_env_decay_accent_ : vcf_env_decay_normal_;
    vcf_env_level_ = vcf_env_span_;
    vcf_env_pos_ = kFilterEnvRecalcInterval;

    if (use_accent)
    {
      // Fixed short MEG + Accent Sweep into filter; VCA gets MEG feed.
      triggerAccentSweep();
      vca_target_ = 0.5f * (1.f + accent_norm_ * kAccentVcaBoost);
    }
    else
    {
      clearAccentSweep();
      vca_target_ = 0.5f;
    }

    if (arriving_via_slide && vco_phase_inc_ > 0.f)
    {
      slide_active_ = true;
      vca_mode_ = 0;
      return;
    }

    slide_active_ = false;
    vco_pitch_ = vco_pitch_target_;
    vco_phase_inc_ = noteToPhaseInc(vco_pitch_);
    vca_mode_ = 0;
  }

  void advanceClockOneSample()
  {
    samples_until_tick_ -= 1.f;
    while (samples_until_tick_ <= 0.f)
    {
      samples_until_tick_ += samples_per_tick_;
      step_index_ = (step_index_ + 1U) % kStepsPerBar;
      triggerStep(step_index_, true);
    }
  }

  void advanceAccentSweep()
  {
    if (accent_sweep_ < accent_sweep_target_)
    {
      accent_sweep_ += (accent_sweep_target_ - accent_sweep_) * accent_attack_coeff_;
      if (accent_sweep_ > accent_sweep_target_ * 0.98f)
        accent_sweep_target_ = 0.f;
    }
    else
    {
      accent_sweep_ *= accent_release_coeff_;
      if (accent_sweep_ < 0.0001f)
        accent_sweep_ = 0.f;
      accent_cap_ *= 0.985f;
    }
  }

  void updateFilterCoefficients()
  {
    advanceAccentSweep();

    const float sweep_into_filter =
        accent_sweep_ * kAccentSweepDepth * (0.25f + 0.75f * resonance_norm_) * accent_norm_;
    float w = vcf_env_end0_ + vcf_env_level_ + sweep_into_filter;
    w = clipRange(w, 0.0002f, 1.2f);

    float res_coeff = vcf_res_coeff_;
    if (res_coeff < 0.05f)
      res_coeff = 0.05f;

    float k = fasterexpf(-w / res_coeff);
    k = clipRange(k, 0.05f, 0.98f);
    vcf_env_level_ *= vcf_env_decay_;
    vcf_a_ = 2.f * fastercosfullf(2.f * w) * k;
    vcf_b_ = -k * k;
    vcf_c_ = 1.f - vcf_a_ - vcf_b_;
    vcf_env_pos_ = 0U;
  }

  void retuneCurrentNote()
  {
    if (current_degree_ < 0)
      return;

    vco_pitch_target_ = static_cast<float>(root_note_ + current_degree_);
    if (!slide_active_)
    {
      vco_pitch_ = vco_pitch_target_;
      vco_phase_inc_ = noteToPhaseInc(vco_pitch_);
    }
  }

  void advanceSlide()
  {
    if (!slide_active_)
      return;

    vco_pitch_ += (vco_pitch_target_ - vco_pitch_) * slide_coeff_;
    vco_phase_inc_ = noteToPhaseInc(vco_pitch_);
    float pitch_error = vco_pitch_target_ - vco_pitch_;
    if (pitch_error < 0.f)
      pitch_error = -pitch_error;
    if (pitch_error < kSlideSettleSemitones)
    {
      vco_pitch_ = vco_pitch_target_;
      vco_phase_inc_ = noteToPhaseInc(vco_pitch_);
      slide_active_ = false;
    }
  }

  float renderSample()
  {
    if (gate_off_requested_ && vca_mode_ == 2 && vca_level_ <= 0.f)
      return 0.f;

    if (vcf_env_pos_ >= kFilterEnvRecalcInterval)
      updateFilterCoefficients();

    advanceSlide();

    const float oscillator = square_wave_ ? ((vco_phase_ >= 0.f) ? 0.5f : -0.5f) : vco_phase_;
    float filtered = vcf_a_ * vcf_delay1_ + vcf_b_ * vcf_delay2_ + vcf_c_ * oscillator;
    filtered = clipRange(filtered, -2.f, 2.f);
    vcf_delay2_ = vcf_delay1_;
    vcf_delay1_ = filtered;
    ++vcf_env_pos_;

    vco_phase_ += vco_phase_inc_;
    if (vco_phase_ > 0.5f)
      vco_phase_ -= 1.f;

    if (vca_mode_ == 0)
      vca_level_ += (vca_target_ - vca_level_) * vca_attack_;
    else if (vca_mode_ == 1)
    {
      vca_level_ *= vca_decay_;
      if (vca_level_ < (1.f / 65536.f))
      {
        vca_level_ = 0.f;
        vca_mode_ = 2;
      }
    }

    if (gate_off_requested_)
    {
      vca_mode_ = 1;
      gate_off_requested_ = false;
    }

    float amp = vca_level_;
    if (accent_active_)
      amp += accent_sweep_ * accent_norm_ * kAccentEnvFeed;
    amp = clipRange(amp, 0.f, 1.35f);

    const float voiced = filtered * amp;
    const float blocked = voiced - dc_prev_in_ + 0.99608f * dc_prev_out_;
    dc_prev_in_ = voiced;
    dc_prev_out_ = blocked;
    return blocked;
  }

  Step phrase_[kStepsPerBar];
  uint32_t phrase_seed_ = 1U;
  uint32_t step_index_ = 0U;
  float cutoff_norm_ = 0.62f;
  float resonance_norm_ = 0.55f;
  float mix_ = 1.f;
  float env_mod_norm_ = 0.6f;
  float decay_norm_ = 0.4f;
  float accent_norm_ = 0.55f;
  bool square_wave_ = false;
  int8_t root_note_ = 36;
  int8_t current_degree_ = -1;
  float bpm_ = 120.f;
  float samples_per_tick_ = 6000.f;
  float samples_until_tick_ = 6000.f;
  float slide_coeff_ = 0.00095f;
  float vco_phase_inc_ = 0.f;
  float vco_pitch_ = 36.f;
  float vco_pitch_target_ = 36.f;
  float vco_phase_ = 0.f;
  float vcf_cutoff_ = 0.f;
  float vcf_env_mod_ = 0.f;
  float vcf_reso_ = 0.f;
  float vcf_res_coeff_ = 0.f;
  float vcf_env_decay_ = 0.f;
  float vcf_env_decay_normal_ = 0.f;
  float vcf_env_decay_accent_ = 0.f;
  float vcf_a_ = 0.f;
  float vcf_b_ = 0.f;
  float vcf_c_ = 1.f;
  float vcf_delay1_ = 0.f;
  float vcf_delay2_ = 0.f;
  float vcf_env_level_ = 0.f;
  float vcf_env_end0_ = 0.f;
  float vcf_env_span_ = 0.f;
  float accent_sweep_ = 0.f;
  float accent_sweep_target_ = 0.f;
  float accent_cap_ = 0.f;
  float accent_attack_coeff_ = 0.2f;
  float accent_release_coeff_ = 0.9f;
  float vca_level_ = 0.f;
  float vca_attack_ = 0.05f;
  float vca_decay_ = 0.999f;
  float vca_target_ = 0.5f;
  float dc_prev_in_ = 0.f;
  float dc_prev_out_ = 0.f;
  uint32_t vcf_env_pos_ = kFilterEnvRecalcInterval;
  int vca_mode_ = 2;
  bool running_ = false;
  bool gate_off_requested_ = false;
  bool slide_active_ = false;
  bool accent_active_ = false;
};
