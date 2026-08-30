#pragma once

/*
 * File: kaocid.h
 *
 * TB-303 style monophonic acid bass with automatic 16-step phrase generator
 * for NTS-3 kaoss pad. Hold the pad to run the sequencer; each new touch
 * regenerates a random acid pattern with guaranteed pitch glides. X = cutoff,
 * Y = resonance, Depth = mix. ROOT sets the phrase key.
 *
 * Panel knobs follow the TB-303: waveform, cutoff, resonance, env mod, decay,
 * accent. Distortion/delay/reverb are left to other NTS-3 slots.
 *
 * NTS-3 genericfx cannot resolve libm (expf/cosf/powf). Pitch and envelopes
 * use logue-sdk float_math.h. The voice is a naive saw/square into a
 * saturating Chamberlin SVF, then VCA — analog 303 order (VCF then VCA),
 * not gsynth's VCA-into-filter path which collapses to filter pings after
 * the envelope dies.
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
  static constexpr uint32_t kSvfRecalcInterval = 32U;
  static constexpr float kOutputGain = 0.45f;
  static constexpr uint32_t kMinGlidesPerPhrase = 3U;
  static constexpr float kAccentVcaRange = 0.7f;
  static constexpr float kAccentEnvOctaves = 1.15f;
  static constexpr float kSlideTauSec = 100000.f * 0.00000022f;
  static constexpr float kSlideSettleSemitones = 0.01f;
  static constexpr float kVcaAttackSec = 0.004f;
  static constexpr float kVcaReleaseSec = 0.045f;
  static constexpr float kMinCutoffHz = 40.f;
  static constexpr float kMaxCutoffHz = 7200.f;

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
    // Retriggering here turns every 16th into an attack click.
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
    svf_low_ = 0.f;
    svf_band_ = 0.f;
    svf_f_ = 0.2f;
    svf_q_ = 1.f;
    svf_pos_ = kSvfRecalcInterval;
    vcf_env_level_ = 0.f;
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
    cutoff_hz_ = kMinCutoffHz * fasterpow2f(cutoff_norm_ * 7.4f);
    env_octaves_ = 0.35f + env_mod_norm_ * 4.1f;
    svf_q_ = 2.f - resonance_norm_ * 1.82f;
    if (svf_q_ < 0.18f)
      svf_q_ = 0.18f;

    const float decay_seconds = 0.2f + 2.3f * decay_norm_;
    vcf_env_decay_ = fasterexpf(-2.3025851f / (decay_seconds * getSampleRate()));
    svf_pos_ = kSvfRecalcInterval;
  }

  void generatePhrase(uint32_t seed)
  {
    uint32_t rng = seed;

    static const int8_t kScaleDegrees[] = {0, 3, 5, 7, 10, 12, 15, 17};
    static constexpr uint32_t kScaleLength = sizeof(kScaleDegrees) / sizeof(kScaleDegrees[0]);

    const uint32_t pulse_count = 7U + (nextRandom(rng) % 4U);

    for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
    {
      Step &step = phrase_[stepIndex];
      step.accent = false;
      step.slide = false;
      step.degree = -1;

      if (!euclideanGate(stepIndex, pulse_count, kStepsPerBar))
        continue;

      const uint32_t degree_index = nextRandom(rng) % kScaleLength;
      step.degree = kScaleDegrees[degree_index];

      if ((stepIndex % 4U) == 0U || randomFloat(rng) > 0.62f)
        step.accent = true;
    }

    if (phrase_[0].degree < 0)
    {
      phrase_[0].degree = 0;
      phrase_[0].accent = true;
    }

    uint32_t glide_count = 0U;
    for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
    {
      const uint32_t next_index = (stepIndex + 1U) % kStepsPerBar;
      if (phrase_[stepIndex].degree < 0 || phrase_[next_index].degree < 0)
        continue;

      if (randomFloat(rng) < 0.42f)
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
        phrase_[next_index].degree = 12;

      applyGlideLeap(search_index, next_index, rng);
      phrase_[search_index].slide = true;
      ++glide_count;
    }

    for (uint32_t force_index = 0; glide_count < kMinGlidesPerPhrase && force_index < kStepsPerBar;
         force_index += 4U)
    {
      if (phrase_[force_index].degree < 0)
        phrase_[force_index].degree = 0;

      const uint32_t next_index = (force_index + 1U) % kStepsPerBar;
      if (phrase_[next_index].degree < 0)
        phrase_[next_index].degree = 12;

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
      phrase_[dest_index].degree = dest_degree;
    }
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

    slide_active_ = false;
    vco_pitch_ = vco_pitch_target_;
    vco_phase_inc_ = noteToPhaseInc(vco_pitch_);
    accent_active_ = step.accent;
    vca_target_ = 0.5f;
    if (step.accent)
      vca_target_ = 0.5f * (1.f + accent_norm_ * kAccentVcaRange);
    vca_level_ = 0.f;
    vca_mode_ = 0;
    vcf_env_level_ = 1.f;
    svf_pos_ = kSvfRecalcInterval;
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

  void updateSvfCoeffs()
  {
    float env_oct = vcf_env_level_ * env_octaves_;
    if (accent_active_)
      env_oct += vcf_env_level_ * accent_norm_ * kAccentEnvOctaves;

    float cutoff_hz = cutoff_hz_ * fasterpow2f(env_oct);
    cutoff_hz = clipRange(cutoff_hz, kMinCutoffHz, kMaxCutoffHz);

    svf_f_ = cutoff_hz * (6.2831853f / getSampleRate());
    if (svf_f_ > 0.85f)
      svf_f_ = 0.85f;
    if (svf_f_ > svf_q_)
      svf_f_ = svf_q_;
    svf_pos_ = 0U;
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

    if (svf_pos_ >= kSvfRecalcInterval)
      updateSvfCoeffs();

    advanceSlide();

    const float oscillator = square_wave_ ? ((vco_phase_ >= 0.f) ? 0.5f : -0.5f) : vco_phase_;

    svf_low_ += svf_f_ * svf_band_;
    const float high = oscillator - svf_low_ - svf_q_ * svf_band_;
    svf_band_ += svf_f_ * high;
    svf_low_ = clipRange(svf_low_, -1.8f, 1.8f);
    svf_band_ = clipRange(svf_band_, -1.8f, 1.8f);
    ++svf_pos_;

    vco_phase_ += vco_phase_inc_;
    if (vco_phase_ > 0.5f)
      vco_phase_ -= 1.f;

    vcf_env_level_ *= vcf_env_decay_;

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

    const float voiced = svf_low_ * vca_level_;
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
  float cutoff_hz_ = 400.f;
  float env_octaves_ = 2.8f;
  float vco_phase_inc_ = 0.f;
  float vco_pitch_ = 36.f;
  float vco_pitch_target_ = 36.f;
  float vco_phase_ = 0.f;
  float svf_f_ = 0.2f;
  float svf_q_ = 1.f;
  float svf_low_ = 0.f;
  float svf_band_ = 0.f;
  float vcf_env_decay_ = 0.999f;
  float vcf_env_level_ = 0.f;
  float vca_level_ = 0.f;
  float vca_attack_ = 0.05f;
  float vca_decay_ = 0.999f;
  float vca_target_ = 0.5f;
  float dc_prev_in_ = 0.f;
  float dc_prev_out_ = 0.f;
  uint32_t svf_pos_ = kSvfRecalcInterval;
  int vca_mode_ = 2;
  bool running_ = false;
  bool gate_off_requested_ = false;
  bool slide_active_ = false;
  bool accent_active_ = false;
};
