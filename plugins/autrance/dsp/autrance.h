#pragma once

/*
 * File: autrance.h
 *
 * Polyphonic trance pluck phrase generator for NTS-3 kaoss pad. Hold the pad
 * to run a 16-step sequencer; each new touch regenerates root rhythm and chord
 * extension layers independently. Chord hits add 9th, 11th, and 13th intervals
 * above the active root degree. X = cutoff, Y = resonance, Depth = mix.
 */

#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class Autrance : public Processor
{
public:
  static constexpr uint32_t kStepsPerBar = 16U;
  static constexpr uint32_t kMaxVoices = 8U;
  static constexpr float kOutputGain = 0.62f;
  static constexpr float kNinthInterval = 14.f;
  static constexpr float kEleventhInterval = 17.f;
  static constexpr float kThirteenthInterval = 21.f;
  static constexpr float kAttackSec = 0.004f;
  static constexpr float kReleaseSec = 0.05f;
  static constexpr float kMinDecaySec = 0.12f;
  static constexpr float kMaxDecaySec = 0.65f;
  static constexpr float kParamSmoothCoeff = 0.0015f;
  static constexpr uint32_t kPhraseStyleCount = 6U;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    CUT = 0U,
    RES,
    MIX,
    DEC,
    ENV,
    ROOT,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case CUT:
      cutoff_norm_target_ = param_10bit_to_f32(value);
      break;
    case RES:
      resonance_norm_target_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = value / 1000.f;
      if (mix_ < 0.f)
        mix_ = 0.f;
      if (mix_ > 1.f)
        mix_ = 1.f;
      break;
    case DEC:
      decay_norm_ = param_10bit_to_f32(value);
      updateDecayCoeff();
      break;
    case ENV:
      env_mod_norm_ = param_10bit_to_f32(value);
      break;
    case ROOT:
    {
      int32_t midi_note = value;
      if (midi_note < 24)
        midi_note = 24;
      if (midi_note > 48)
        midi_note = 48;
      root_note_ = static_cast<int8_t>(midi_note);
      break;
    }
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
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
    cutoff_norm_target_ = 0.58f;
    resonance_norm_target_ = 0.42f;
    cutoff_norm_smooth_ = 0.58f;
    resonance_norm_smooth_ = 0.42f;
    mix_ = 1.f;
    decay_norm_ = 0.45f;
    env_mod_norm_ = 0.65f;
    root_note_ = 36;
    bpm_ = 120.f;
    const float sample_rate = getSampleRate();
    attack_step_ = 1.f / (kAttackSec * sample_rate);
    updateDecayCoeff();
    samples_per_tick_ = sample_rate * 15.f / bpm_;
    running_ = false;
    phrase_seed_ = 1U;
    active_root_degree_ = 0;
    next_voice_index_ = 0U;
    resetVoices();
    generatePhrase(phrase_seed_);
  }

  void reset() override final
  {
    running_ = false;
    resetVoices();
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 20.f && tempo < 999.f)
    {
      bpm_ = tempo;
      samples_per_tick_ = getSampleRate() * 15.f / bpm_;
      updateDecayCoeff();
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
      releaseAllVoices();
      return;
    }

    if (phase != k_unit_touch_phase_began && phase != k_unit_touch_phase_moved &&
        phase != k_unit_touch_phase_stationary)
      return;

    if (running_)
      return;

    phrase_seed_ = mixSeed(phrase_seed_, x, y);
    generatePhrase(phrase_seed_);
    resetVoices();
    step_index_ = 0U;
    samples_until_tick_ = samples_per_tick_;
    active_root_degree_ = 0;
    running_ = true;
    triggerStep(0U);
  }

  void smoothPanelParams()
  {
    cutoff_norm_smooth_ += (cutoff_norm_target_ - cutoff_norm_smooth_) * kParamSmoothCoeff;
    resonance_norm_smooth_ += (resonance_norm_target_ - resonance_norm_smooth_) * kParamSmoothCoeff;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    const float dry_gain = 1.f - mix_;
    const float wet_gain = mix_ * kOutputGain;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      smoothPanelParams();

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
  struct RootStep
  {
    int8_t degree = -1;
  };

  struct ChordStep
  {
    bool ninth = false;
    bool eleventh = false;
    bool thirteenth = false;
  };

  struct Voice
  {
    bool active = false;
    bool releasing = false;
    float phase = 0.f;
    float phase_inc = 0.f;
    float env = 0.f;
    float velocity = 1.f;
    float filter_state = 0.f;
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

  void updateDecayCoeff()
  {
    const float decay_sec = kMinDecaySec + decay_norm_ * (kMaxDecaySec - kMinDecaySec);
    const float sample_rate = getSampleRate();
    amp_decay_coeff_ = fasterexpf(-1.f / (decay_sec * sample_rate));
    release_coeff_ = fasterexpf(-1.f / (kReleaseSec * sample_rate));
  }

  void releaseVoice(Voice &voice)
  {
    if (!voice.active)
      return;
    voice.releasing = true;
  }

  void releaseAllVoices()
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
      releaseVoice(voices_[voiceIndex]);
  }

  void resetVoices()
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
      voices_[voiceIndex] = Voice();
    step_index_ = 0U;
    samples_until_tick_ = samples_per_tick_;
  }

  void clearPhrase()
  {
    for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
    {
      root_steps_[stepIndex].degree = -1;
      chord_steps_[stepIndex].ninth = false;
      chord_steps_[stepIndex].eleventh = false;
      chord_steps_[stepIndex].thirteenth = false;
    }
  }

  int8_t randomScaleDegree(uint32_t &rng, uint32_t scale_id) const
  {
    static const int8_t kMajor[] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16};
    static const int8_t kNaturalMinor[] = {0, 2, 3, 5, 7, 8, 10, 12, 14};
    static const int8_t kMinorPent[] = {0, 3, 5, 7, 10, 12, 15};
    static const int8_t kDorian[] = {0, 2, 3, 5, 7, 9, 10, 12, 14};
    static const int8_t kMajorPent[] = {0, 2, 4, 7, 9, 12, 14};
    static const int8_t kHarmonicMinor[] = {0, 2, 3, 5, 7, 8, 11, 12};

    const int8_t *degrees = kMajor;
    uint32_t length = 10U;
    switch (scale_id % 6U)
    {
    case 1:
      degrees = kNaturalMinor;
      length = 9U;
      break;
    case 2:
      degrees = kMinorPent;
      length = 7U;
      break;
    case 3:
      degrees = kDorian;
      length = 9U;
      break;
    case 4:
      degrees = kMajorPent;
      length = 7U;
      break;
    case 5:
      degrees = kHarmonicMinor;
      length = 8U;
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

  void assignChordExtensions(uint32_t &rng, uint32_t stepIndex, uint32_t style_id)
  {
    ChordStep &step = chord_steps_[stepIndex];
    step.ninth = false;
    step.eleventh = false;
    step.thirteenth = false;

    switch (style_id % 4U)
    {
    case 0:
      step.ninth = true;
      step.eleventh = randomFloat(rng) > 0.35f;
      step.thirteenth = randomFloat(rng) > 0.45f;
      break;
    case 1:
      step.ninth = randomFloat(rng) > 0.25f;
      step.eleventh = true;
      step.thirteenth = randomFloat(rng) > 0.4f;
      break;
    case 2:
      step.ninth = true;
      step.eleventh = true;
      step.thirteenth = true;
      break;
    default:
      if (randomFloat(rng) > 0.5f)
        step.ninth = true;
      else if (randomFloat(rng) > 0.5f)
        step.eleventh = true;
      else
        step.thirteenth = true;
      break;
    }
  }

  void generateRootLayer(uint32_t &rng, uint32_t style, uint32_t scale_id)
  {
    switch (style % kPhraseStyleCount)
    {
    case 0:
    {
      const uint32_t pulses = 4U + randomIndex(rng, 7U);
      for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
      {
        if (!euclideanGate(stepIndex, pulses, kStepsPerBar))
          continue;
        root_steps_[stepIndex].degree = randomScaleDegree(rng, scale_id);
      }
      break;
    }
    case 1:
    {
      const uint32_t hit_count = 3U + randomIndex(rng, 5U);
      uint32_t placed = 0U;
      uint32_t guard = 0U;
      while (placed < hit_count && guard < 48U)
      {
        const uint32_t stepIndex = randomIndex(rng, kStepsPerBar);
        ++guard;
        if (root_steps_[stepIndex].degree >= 0)
          continue;
        root_steps_[stepIndex].degree = randomScaleDegree(rng, scale_id);
        ++placed;
      }
      break;
    }
    case 2:
      for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; stepIndex += 2U)
        root_steps_[stepIndex].degree = randomScaleDegree(rng, scale_id);
      break;
    case 3:
    {
      const uint32_t gallop = randomIndex(rng, 3U);
      for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
      {
        const uint32_t cell = stepIndex % 4U;
        bool hit = false;
        if (gallop == 0U)
          hit = cell != 3U;
        else if (gallop == 1U)
          hit = cell < 2U;
        else
          hit = cell != 1U;
        if (!hit)
          continue;
        root_steps_[stepIndex].degree = randomScaleDegree(rng, scale_id);
      }
      break;
    }
    case 4:
    {
      int32_t cursor = (randomFloat(rng) > 0.5f) ? 0 : 7;
      const int32_t walk = (cursor == 0) ? 2 : -2;
      for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
      {
        if ((stepIndex % 3U) == 2U && randomFloat(rng) > 0.35f)
          continue;
        root_steps_[stepIndex].degree = clampDegree(cursor);
        cursor += walk;
      }
      break;
    }
    default:
    {
      const int8_t motif_a = randomScaleDegree(rng, scale_id);
      int8_t motif_b = randomScaleDegree(rng, scale_id);
      if (motif_b == motif_a)
        motif_b = clampDegree(motif_a + 7);
      for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
        root_steps_[stepIndex].degree = ((stepIndex & 1U) == 0U) ? motif_a : motif_b;
      break;
    }
    }
  }

  void generateChordLayer(uint32_t &rng, uint32_t style, uint32_t chord_style)
  {
    switch (style % kPhraseStyleCount)
    {
    case 0:
    {
      const uint32_t pulses = 2U + randomIndex(rng, 5U);
      for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
      {
        if (!euclideanGate(stepIndex, pulses, kStepsPerBar))
          continue;
        assignChordExtensions(rng, stepIndex, chord_style);
      }
      break;
    }
    case 1:
    {
      for (uint32_t stepIndex = 1; stepIndex < kStepsPerBar; stepIndex += 4U)
        assignChordExtensions(rng, stepIndex, chord_style);
      if (randomFloat(rng) > 0.4f)
        assignChordExtensions(rng, 3U, chord_style);
      break;
    }
    case 2:
      for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
      {
        if ((stepIndex & 1U) != 0U)
          assignChordExtensions(rng, stepIndex, chord_style);
      }
      break;
    case 3:
    {
      const uint32_t offset = randomIndex(rng, 4U);
      for (uint32_t stepIndex = offset; stepIndex < kStepsPerBar; stepIndex += 4U)
        assignChordExtensions(rng, stepIndex, chord_style);
      break;
    }
    case 4:
    {
      uint32_t placed = 0U;
      const uint32_t target = 2U + randomIndex(rng, 4U);
      uint32_t guard = 0U;
      while (placed < target && guard < 32U)
      {
        const uint32_t stepIndex = randomIndex(rng, kStepsPerBar);
        ++guard;
        const ChordStep &existing = chord_steps_[stepIndex];
        if (existing.ninth || existing.eleventh || existing.thirteenth)
          continue;
        assignChordExtensions(rng, stepIndex, chord_style);
        ++placed;
      }
      break;
    }
    default:
      for (uint32_t stepIndex = 2; stepIndex < kStepsPerBar; stepIndex += 3U)
        assignChordExtensions(rng, stepIndex, chord_style);
      break;
    }
  }

  void ensureRootActivity(uint32_t &rng, uint32_t scale_id)
  {
    for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
    {
      if (root_steps_[stepIndex].degree >= 0)
        return;
    }
    const uint32_t fallback = randomIndex(rng, kStepsPerBar);
    root_steps_[fallback].degree = randomScaleDegree(rng, scale_id);
  }

  void generatePhrase(uint32_t seed)
  {
    uint32_t rng = seed;
    clearPhrase();

    const uint32_t root_style = randomIndex(rng, kPhraseStyleCount);
    const uint32_t chord_style = randomIndex(rng, kPhraseStyleCount);
    const uint32_t scale_id = randomIndex(rng, 6U);
    const uint32_t extension_style = randomIndex(rng, 4U);

    generateRootLayer(rng, root_style, scale_id);
    generateChordLayer(rng, chord_style, extension_style);
    ensureRootActivity(rng, scale_id);
  }

  void startVoice(float midi_note, float velocity)
  {
    Voice &voice = voices_[next_voice_index_];
    next_voice_index_ = (next_voice_index_ + 1U) % kMaxVoices;

    voice.active = true;
    voice.releasing = false;
    voice.phase = 0.f;
    voice.phase_inc = noteToPhaseInc(midi_note);
    voice.env = 0.f;
    voice.velocity = clipRange(velocity, 0.25f, 1.f);
    voice.filter_state = 0.f;
  }

  void triggerRoot(int8_t degree)
  {
    if (degree < 0)
      return;

    active_root_degree_ = degree;
    const float midi_note = static_cast<float>(root_note_ + degree);
    startVoice(midi_note, 0.95f);
  }

  void triggerChordExtensions(int8_t degree, const ChordStep &chord)
  {
    if (degree < 0)
      degree = active_root_degree_;

    const float root_pitch = static_cast<float>(root_note_ + degree);
    if (chord.ninth)
      startVoice(root_pitch + kNinthInterval, 0.72f);
    if (chord.eleventh)
      startVoice(root_pitch + kEleventhInterval, 0.68f);
    if (chord.thirteenth)
      startVoice(root_pitch + kThirteenthInterval, 0.64f);
  }

  void triggerStep(uint32_t step_index)
  {
    const RootStep &root = root_steps_[step_index];
    const ChordStep &chord = chord_steps_[step_index];
    const bool has_chord = chord.ninth || chord.eleventh || chord.thirteenth;

    if (root.degree < 0 && !has_chord)
      return;

    if (root.degree >= 0)
      triggerRoot(root.degree);

    const int8_t chord_degree = (root.degree >= 0) ? root.degree : active_root_degree_;
    if (has_chord)
      triggerChordExtensions(chord_degree, chord);
  }

  void advanceClockOneSample()
  {
    samples_until_tick_ -= 1.f;
    uint32_t guard = 0U;
    while (samples_until_tick_ <= 0.f && guard < 4U)
    {
      samples_until_tick_ += samples_per_tick_;
      step_index_ = (step_index_ + 1U) % kStepsPerBar;
      triggerStep(step_index_);
      ++guard;
    }
  }

  float renderVoice(Voice &voice, float cutoff_base_hz, float resonance_q, float env_feed)
  {
    if (!voice.active)
      return 0.f;

    if (voice.releasing)
      voice.env *= release_coeff_;
    else if (voice.env < 1.f)
    {
      voice.env += attack_step_;
      if (voice.env > 1.f)
        voice.env = 1.f;
    }
    else
      voice.env *= amp_decay_coeff_;

    if (voice.env < 0.0001f)
    {
      voice.active = false;
      voice.env = 0.f;
      voice.filter_state = 0.f;
      return 0.f;
    }

    voice.phase += voice.phase_inc;
    if (voice.phase >= 1.f)
      voice.phase -= 1.f;
    const float saw = 2.f * voice.phase - 1.f;

    const float filter_track = 0.15f + voice.env * (0.35f + env_mod_norm_ * env_feed * 0.5f);
    const float cutoff_hz = clipRange(cutoff_base_hz * filter_track, 150.f, 14000.f);
    const float omega = 6.2831853f * cutoff_hz / getSampleRate();
    const float filter_coeff = clipRange(omega / (1.f + omega), 0.002f, 0.96f);
    const float resonance_mix = clipRange(resonance_q * 0.1f, 0.f, 0.75f);

    voice.filter_state += filter_coeff * (saw - voice.filter_state);
    const float filtered = voice.filter_state + resonance_mix * (saw - voice.filter_state);
    return filtered * voice.env * voice.velocity;
  }

  float renderSample()
  {
    const float cutoff_hz = 300.f + cutoff_norm_smooth_ * 7200.f;
    const float resonance_q = 0.35f + resonance_norm_smooth_ * 8.5f;
    const float env_feed = 1.f + env_mod_norm_ * 4.f;

    float wet = 0.f;
    for (uint32_t voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
      wet += renderVoice(voices_[voiceIndex], cutoff_hz, resonance_q, env_feed);

    return wet;
  }

  RootStep root_steps_[kStepsPerBar];
  ChordStep chord_steps_[kStepsPerBar];
  Voice voices_[kMaxVoices];
  uint32_t phrase_seed_ = 1U;
  uint32_t step_index_ = 0U;
  uint32_t next_voice_index_ = 0U;
  float cutoff_norm_target_ = 0.58f;
  float resonance_norm_target_ = 0.42f;
  float cutoff_norm_smooth_ = 0.58f;
  float resonance_norm_smooth_ = 0.42f;
  float mix_ = 1.f;
  float decay_norm_ = 0.45f;
  float env_mod_norm_ = 0.65f;
  int8_t root_note_ = 36;
  int8_t active_root_degree_ = 0;
  float bpm_ = 120.f;
  float samples_per_tick_ = 6000.f;
  float samples_until_tick_ = 6000.f;
  float attack_step_ = 0.005f;
  float amp_decay_coeff_ = 0.9995f;
  float release_coeff_ = 0.998f;
  bool running_ = false;
};
