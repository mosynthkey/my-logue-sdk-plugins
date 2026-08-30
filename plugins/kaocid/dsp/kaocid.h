#pragma once

/*
 * File: kaocid.h
 *
 * TB-303 style monophonic acid bass with automatic 16-step phrase generator
 * for NTS-3 kaoss pad. Hold the pad to run the sequencer; each new touch
 * regenerates a legato acid line (long tied runs, short rests, pitch glides). X = cutoff,
 * Y = resonance, Depth = mix. ROOT sets the phrase key.
 *
 * Panel knobs follow the TB-303: waveform, cutoff, resonance, env mod, decay,
 * accent. Distortion/delay/reverb are left to other NTS-3 slots.
 *
 * Filter coefficients follow gsynth TB-303 (Andy Sloane, 2001), adapted for
 * 48 kHz. NTS-3 genericfx cannot resolve libm, so pitch/env/filter use
 * logue-sdk float_math.h. VCA is after the VCF (analog 303 order) so the
 * note body remains after the envelope; gsynth fed the VCA into the filter
 * which collapsed to attack pings on device.
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
  static constexpr uint32_t kMinGlidesPerPhrase = 3U;
  static constexpr uint32_t kMinActiveStepsPerPhrase = 13U;
  static constexpr uint32_t kMaxRestGapSteps = 2U;
  static constexpr float kAccentVcaRange = 0.7f;
  static constexpr float kAccentCutoffRange = 0.42f;
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
  uint32_t debugActiveStepCount() const
  {
    uint32_t active_count = 0U;
    for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
    {
      if (phrase_[stepIndex].degree >= 0)
        ++active_count;
    }
    return active_count;
  }
  uint32_t debugLegatoPairCount() const
  {
    uint32_t legato_count = 0U;
    for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
    {
      const uint32_t next_index = (stepIndex + 1U) % kStepsPerBar;
      if (phrase_[stepIndex].degree >= 0 && phrase_[next_index].degree >= 0)
        ++legato_count;
    }
    return legato_count;
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

  static float randomFloat(uint32_t &state)
  {
    return static_cast<float>(nextRandom(state) >> 8) * (1.f / 16777216.f);
  }

  static int8_t clampDegree(int8_t degree)
  {
    if (degree < 0)
      return 0;
    if (degree > 17)
      return 17;
    return degree;
  }

  static int8_t pickWalkDegree(int8_t current_degree, uint32_t &rng)
  {
    static const int8_t kWalkDeltas[] = {-5, -3, -2, -1, 1, 2, 3, 5};
    static constexpr uint32_t kWalkCount = sizeof(kWalkDeltas) / sizeof(kWalkDeltas[0]);
    return clampDegree(static_cast<int8_t>(current_degree + kWalkDeltas[nextRandom(rng) % kWalkCount]));
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
    vcf_cutoff_ = 0.12f + cutoff_norm_ * 0.78f;
    if (vcf_cutoff_ > 1.f)
      vcf_cutoff_ = 1.f;
    vcf_reso_ = 0.05f + resonance_norm_ * 0.9f;
    if (vcf_reso_ > 1.f)
      vcf_reso_ = 1.f;
    vcf_env_mod_ = 0.08f + env_mod_norm_ * 0.92f;

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

    const float decay_seconds = 0.2f + 2.3f * decay_norm_;
    const float decay_samples = decay_seconds * getSampleRate();
    vcf_env_decay_ = fasterexpf(-2.3025851f * static_cast<float>(kFilterEnvRecalcInterval) / decay_samples);
    vcf_env_pos_ = kFilterEnvRecalcInterval;
  }

  void generatePhrase(uint32_t seed)
  {
    uint32_t rng = seed;

    static const int8_t kScaleDegrees[] = {0, 3, 5, 7, 10, 12, 15, 17};
    static constexpr uint32_t kScaleLength = sizeof(kScaleDegrees) / sizeof(kScaleDegrees[0]);

    bool active[kStepsPerBar];
    for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
      active[stepIndex] = true;

    const uint32_t gap_length = 1U + (nextRandom(rng) & 1U);
    uint32_t gap_start = 12U + (nextRandom(rng) % 3U);
    if (gap_start + gap_length > kStepsPerBar)
      gap_start = kStepsPerBar - gap_length;

    for (uint32_t gapOffset = 0; gapOffset < gap_length; ++gapOffset)
      active[gap_start + gapOffset] = false;

    active[0] = true;

    int8_t current_degree = kScaleDegrees[nextRandom(rng) % kScaleLength];
    bool after_rest = true;

    for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
    {
      Step &step = phrase_[stepIndex];
      step.accent = false;
      step.slide = false;
      step.degree = -1;

      if (!active[stepIndex])
      {
        after_rest = true;
        continue;
      }

      if (after_rest)
        current_degree = kScaleDegrees[nextRandom(rng) % kScaleLength];
      else if (randomFloat(rng) < 0.78f)
        current_degree = pickWalkDegree(current_degree, rng);
      else
        current_degree = kScaleDegrees[nextRandom(rng) % kScaleLength];

      step.degree = current_degree;
      after_rest = false;

      if ((stepIndex % 4U) == 0U)
        step.accent = true;
      else if (randomFloat(rng) > 0.78f)
        step.accent = true;
    }

    phrase_[0].accent = true;

    uint32_t glide_count = 0U;
    for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
    {
      const uint32_t next_index = (stepIndex + 1U) % kStepsPerBar;
      if (phrase_[stepIndex].degree < 0 || phrase_[next_index].degree < 0)
        continue;

      int32_t interval = phrase_[next_index].degree - phrase_[stepIndex].degree;
      if (interval < 0)
        interval = -interval;

      if (interval >= 5)
      {
        phrase_[stepIndex].slide = true;
        ++glide_count;
        continue;
      }

      if (randomFloat(rng) < 0.55f)
        continue;

      applyGlideLeap(stepIndex, next_index, rng);
      phrase_[stepIndex].slide = true;
      ++glide_count;
    }

    for (uint32_t search_index = 0; glide_count < kMinGlidesPerPhrase && search_index < kStepsPerBar;
         ++search_index)
    {
      if (phrase_[search_index].slide || phrase_[search_index].degree < 0)
        continue;

      const uint32_t next_index = (search_index + 1U) % kStepsPerBar;
      if (phrase_[next_index].degree < 0)
        phrase_[next_index].degree = clampDegree(static_cast<int8_t>(phrase_[search_index].degree + 7));

      applyGlideLeap(search_index, next_index, rng);
      phrase_[search_index].slide = true;
      ++glide_count;
    }

    for (uint32_t force_index = 2; glide_count < kMinGlidesPerPhrase && force_index < kStepsPerBar;
         force_index += 3U)
    {
      if (phrase_[force_index].degree < 0)
        continue;

      const uint32_t next_index = (force_index + 1U) % kStepsPerBar;
      if (phrase_[next_index].degree < 0)
        phrase_[next_index].degree = clampDegree(static_cast<int8_t>(phrase_[force_index].degree + 12));

      if (phrase_[force_index].slide)
        continue;

      applyGlideLeap(force_index, next_index, rng);
      phrase_[force_index].slide = true;
      ++glide_count;
    }
  }

  void applyGlideLeap(uint32_t source_index, uint32_t dest_index, uint32_t &rng)
  {
    static const int8_t kGlideLeaps[] = {5, 7, 10, 12, -5, -7, -12};
    static constexpr uint32_t kLeapCount = sizeof(kGlideLeaps) / sizeof(kGlideLeaps[0]);

    const int8_t source_degree = phrase_[source_index].degree;
    int8_t dest_degree = phrase_[dest_index].degree;
    int32_t interval = dest_degree - source_degree;
    if (interval < 0)
      interval = -interval;

    if (interval < 5)
    {
      const int8_t leap = kGlideLeaps[nextRandom(rng) % kLeapCount];
      dest_degree = static_cast<int8_t>(source_degree + leap);
      if (dest_degree < 0 || dest_degree > 17)
        dest_degree = static_cast<int8_t>(source_degree - leap);
      if (dest_degree < 0 || dest_degree > 17)
        dest_degree = (source_degree <= 5) ? static_cast<int8_t>(source_degree + 12)
                                           : static_cast<int8_t>(source_degree - 12);
      phrase_[dest_index].degree = clampDegree(dest_degree);
    }
  }

  void triggerStep(uint32_t step_index, bool allow_slide_in)
  {
    const Step &step = phrase_[step_index];
    const uint32_t prev_index = (step_index + kStepsPerBar - 1U) % kStepsPerBar;
    const bool arriving_via_slide = allow_slide_in && phrase_[prev_index].slide &&
                                    phrase_[prev_index].degree >= 0;
    const bool arriving_legato = allow_slide_in && !arriving_via_slide &&
                                 phrase_[prev_index].degree >= 0 && vco_phase_inc_ > 0.f;

    if (step.degree < 0)
    {
      if (!arriving_via_slide)
      {
        gate_off_requested_ = true;
        current_degree_ = -1;
      }
      return;
    }

    current_degree_ = step.degree;
    vco_pitch_target_ = static_cast<float>(root_note_ + step.degree);
    gate_off_requested_ = false;

    if (arriving_via_slide && vco_phase_inc_ > 0.f)
    {
      slide_active_ = true;
      return;
    }

    if (arriving_legato)
    {
      slide_active_ = false;
      vco_pitch_ = vco_pitch_target_;
      vco_phase_inc_ = noteToPhaseInc(vco_pitch_);
      return;
    }

    slide_active_ = false;
    vco_pitch_ = vco_pitch_target_;
    vco_phase_inc_ = noteToPhaseInc(vco_pitch_);
    accent_active_ = step.accent;
    vca_target_ = 0.5f;
    if (step.accent)
      vca_target_ = 0.5f * (1.f + accent_norm_ * kAccentVcaRange);
    vca_mode_ = 0;
    vcf_env_level_ = vcf_env_span_;
    if (accent_active_)
      vcf_env_level_ += vcf_env_span_ * (0.08f + accent_norm_ * kAccentCutoffRange);
    vcf_env_pos_ = kFilterEnvRecalcInterval;
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

  void updateFilterCoefficients()
  {
    float w = vcf_env_end0_ + vcf_env_level_;
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

    const float voiced = filtered * vca_level_;
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
  float vcf_a_ = 0.f;
  float vcf_b_ = 0.f;
  float vcf_c_ = 1.f;
  float vcf_delay1_ = 0.f;
  float vcf_delay2_ = 0.f;
  float vcf_env_level_ = 0.f;
  float vcf_env_end0_ = 0.f;
  float vcf_env_span_ = 0.f;
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
