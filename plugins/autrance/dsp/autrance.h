#pragma once

/*
 * File: autrance.h
 *
 * Polyphonic trance pluck phrase generator for NTS-3 kaoss pad. ROOT sets the
 * fixed bass key; each pad touch builds a chord from that root and generates a
 * 16-step bass + chord phrase. Bass uses the root and one octave up. Chord hits
 * play either a full voicing or an arpeggiated tone. X = cutoff, Y = resonance,
 * Depth = mix, DEC = amp/filter decay, ENV = filter envelope depth.
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
  static constexpr uint32_t kMaxChordTones = 7U;
  static constexpr float kOutputGain = 0.62f;
  static constexpr float kAttackSec = 0.004f;
  static constexpr float kReleaseSec = 0.05f;
  static constexpr float kMinDecaySec = 0.12f;
  static constexpr float kMaxDecaySec = 0.65f;
  static constexpr float kGateFraction = 0.72f;
  static constexpr float kEnvelopeFloor = 0.0001f;
  static constexpr float kEnvelopeLogFloor = -9.21034037f;
  static constexpr float kParamSmoothCoeff = 0.0015f;
  static constexpr uint32_t kPhraseStyleCount = 6U;
  static constexpr uint32_t kChordVoicingCount = 6U;

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
    chord_tone_count_ = 0U;
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
  enum ChordStepMode : uint8_t
  {
    kChordRest = 0U,
    kChordBlock = 1U,
    kChordArpeggio = 2U
  };

  struct BassStep
  {
    bool root = false;
    bool octave_up = false;
  };

  struct ChordStep
  {
    ChordStepMode mode = kChordRest;
    uint8_t arp_tone_index = 0U;
  };

  struct ChordVoicing
  {
    const int8_t *intervals;
    uint8_t count;
  };

  struct Voice
  {
    bool active = false;
    bool releasing = false;
    float phase = 0.f;
    float phase_inc = 0.f;
    float env = 0.f;
    float filter_env = 0.f;
    float velocity = 1.f;
    float gate_samples_remaining = 0.f;
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
    amp_decay_coeff_ = fasterexpf(kEnvelopeLogFloor / (decay_sec * sample_rate));
    release_coeff_ = fasterexpf(kEnvelopeLogFloor / (kReleaseSec * sample_rate));
    filter_decay_coeff_ = fasterexpf(kEnvelopeLogFloor / (decay_sec * sample_rate));
    filter_release_coeff_ = filter_decay_coeff_;
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
      bass_steps_[stepIndex] = BassStep();
      chord_steps_[stepIndex] = ChordStep();
    }
    chord_tone_count_ = 0U;
  }

  ChordVoicing pickChordVoicing(uint32_t voicing_id) const
  {
    static const int8_t kMin7_9[] = {0, 3, 7, 10, 14};
    static const int8_t kMin7_911[] = {0, 3, 7, 10, 14, 17};
    static const int8_t kMin7_Full[] = {0, 3, 7, 10, 14, 17, 21};
    static const int8_t kMaj7_9[] = {0, 4, 7, 11, 14};
    static const int8_t kMaj7_911[] = {0, 4, 7, 11, 14, 17};
    static const int8_t kSus4_7_9[] = {0, 5, 7, 10, 14};

    static const ChordVoicing kVoicings[] = {
        {kMin7_9, 5U},
        {kMin7_911, 6U},
        {kMin7_Full, 7U},
        {kMaj7_9, 5U},
        {kMaj7_911, 6U},
        {kSus4_7_9, 5U},
    };

    return kVoicings[voicing_id % kChordVoicingCount];
  }

  void buildChord(uint32_t &rng)
  {
    const ChordVoicing voicing = pickChordVoicing(randomIndex(rng, kChordVoicingCount));
    chord_tone_count_ = voicing.count;
    if (chord_tone_count_ > kMaxChordTones)
      chord_tone_count_ = kMaxChordTones;

    for (uint32_t toneIndex = 0; toneIndex < chord_tone_count_; ++toneIndex)
      chord_tones_[toneIndex] = voicing.intervals[toneIndex];
  }

  void generateBassLayer(uint32_t &rng, uint32_t style)
  {
    switch (style % kPhraseStyleCount)
    {
    case 0:
    {
      const uint32_t pulses = 4U + randomIndex(rng, 5U);
      for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
      {
        if (!euclideanGate(stepIndex, pulses, kStepsPerBar))
          continue;
        bass_steps_[stepIndex].root = true;
        if ((stepIndex & 1U) == 0U)
          bass_steps_[stepIndex].octave_up = randomFloat(rng) > 0.45f;
      }
      break;
    }
    case 1:
    {
      for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; stepIndex += 4U)
        bass_steps_[stepIndex].root = true;
      for (uint32_t stepIndex = 2; stepIndex < kStepsPerBar; stepIndex += 4U)
        bass_steps_[stepIndex].octave_up = true;
      break;
    }
    case 2:
      for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; stepIndex += 2U)
      {
        if ((stepIndex & 2U) == 0U)
          bass_steps_[stepIndex].root = true;
        else
          bass_steps_[stepIndex].octave_up = true;
      }
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
        if (randomFloat(rng) > 0.55f)
          bass_steps_[stepIndex].octave_up = true;
        else
          bass_steps_[stepIndex].root = true;
      }
      break;
    }
    case 4:
    {
      for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
      {
        if ((stepIndex % 3U) == 2U && randomFloat(rng) > 0.35f)
          continue;
        bass_steps_[stepIndex].root = true;
        if ((stepIndex & 1U) == 1U)
          bass_steps_[stepIndex].octave_up = true;
      }
      break;
    }
    default:
      for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
        bass_steps_[stepIndex].root = ((stepIndex & 1U) == 0U);
      for (uint32_t stepIndex = 1; stepIndex < kStepsPerBar; stepIndex += 4U)
        bass_steps_[stepIndex].octave_up = true;
      break;
    }
  }

  void generateChordLayer(uint32_t &rng, uint32_t style, bool prefer_arpeggio)
  {
    if (chord_tone_count_ == 0U)
      return;

    uint8_t arp_cursor = 0U;

    switch (style % kPhraseStyleCount)
    {
    case 0:
    {
      const uint32_t pulses = 2U + randomIndex(rng, 4U);
      for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
      {
        if (!euclideanGate(stepIndex, pulses, kStepsPerBar))
          continue;
        ChordStep &step = chord_steps_[stepIndex];
        if (prefer_arpeggio && randomFloat(rng) > 0.35f)
        {
          step.mode = kChordArpeggio;
          step.arp_tone_index = arp_cursor;
          arp_cursor = static_cast<uint8_t>((arp_cursor + 1U) % chord_tone_count_);
        }
        else
        {
          step.mode = kChordBlock;
        }
      }
      break;
    }
    case 1:
    {
      for (uint32_t stepIndex = 1; stepIndex < kStepsPerBar; stepIndex += 4U)
      {
        ChordStep &step = chord_steps_[stepIndex];
        step.mode = prefer_arpeggio ? kChordArpeggio : kChordBlock;
        if (step.mode == kChordArpeggio)
        {
          step.arp_tone_index = arp_cursor;
          arp_cursor = static_cast<uint8_t>((arp_cursor + 1U) % chord_tone_count_);
        }
      }
      break;
    }
    case 2:
      for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
      {
        if ((stepIndex & 1U) == 0U)
          continue;
        ChordStep &step = chord_steps_[stepIndex];
        step.mode = kChordArpeggio;
        step.arp_tone_index = arp_cursor;
        arp_cursor = static_cast<uint8_t>((arp_cursor + 1U) % chord_tone_count_);
      }
      break;
    case 3:
    {
      const uint32_t offset = randomIndex(rng, 4U);
      for (uint32_t stepIndex = offset; stepIndex < kStepsPerBar; stepIndex += 4U)
      {
        ChordStep &step = chord_steps_[stepIndex];
        if (prefer_arpeggio || randomFloat(rng) > 0.5f)
        {
          step.mode = kChordArpeggio;
          step.arp_tone_index = arp_cursor;
          arp_cursor = static_cast<uint8_t>((arp_cursor + 1U) % chord_tone_count_);
        }
        else
        {
          step.mode = kChordBlock;
        }
      }
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
        if (chord_steps_[stepIndex].mode != kChordRest)
          continue;
        ChordStep &step = chord_steps_[stepIndex];
        if (prefer_arpeggio && randomFloat(rng) > 0.4f)
        {
          step.mode = kChordArpeggio;
          step.arp_tone_index = arp_cursor;
          arp_cursor = static_cast<uint8_t>((arp_cursor + 1U) % chord_tone_count_);
        }
        else
        {
          step.mode = kChordBlock;
        }
        ++placed;
      }
      break;
    }
    default:
      for (uint32_t stepIndex = 2; stepIndex < kStepsPerBar; stepIndex += 3U)
      {
        ChordStep &step = chord_steps_[stepIndex];
        step.mode = kChordBlock;
      }
      break;
    }
  }

  void ensurePhraseActivity(uint32_t &rng)
  {
    for (uint32_t stepIndex = 0; stepIndex < kStepsPerBar; ++stepIndex)
    {
      if (bass_steps_[stepIndex].root || bass_steps_[stepIndex].octave_up)
        return;
      if (chord_steps_[stepIndex].mode != kChordRest)
        return;
    }

    const uint32_t fallback = randomIndex(rng, kStepsPerBar);
    bass_steps_[fallback].root = true;
    if (chord_tone_count_ > 0U)
      chord_steps_[fallback].mode = kChordBlock;
  }

  void generatePhrase(uint32_t seed)
  {
    uint32_t rng = seed;
    clearPhrase();

    buildChord(rng);

    const uint32_t bass_style = randomIndex(rng, kPhraseStyleCount);
    const uint32_t chord_style = randomIndex(rng, kPhraseStyleCount);
    const bool prefer_arpeggio = randomFloat(rng) > 0.45f;

    generateBassLayer(rng, bass_style);
    generateChordLayer(rng, chord_style, prefer_arpeggio);
    ensurePhraseActivity(rng);
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
    voice.filter_env = 1.f;
    voice.velocity = clipRange(velocity, 0.25f, 1.f);
    voice.gate_samples_remaining = samples_per_tick_ * kGateFraction;
    voice.filter_state = 0.f;
  }

  void triggerBass(const BassStep &bass)
  {
    const float root_pitch = static_cast<float>(root_note_);
    if (bass.root)
      startVoice(root_pitch, 0.92f);
    if (bass.octave_up)
      startVoice(root_pitch + 12.f, 0.84f);
  }

  void triggerChord(const ChordStep &chord)
  {
    if (chord_tone_count_ == 0U || chord.mode == kChordRest)
      return;

    const float root_pitch = static_cast<float>(root_note_);

    if (chord.mode == kChordBlock)
    {
      for (uint32_t toneIndex = 0; toneIndex < chord_tone_count_; ++toneIndex)
        startVoice(root_pitch + static_cast<float>(chord_tones_[toneIndex]), 0.72f);
      return;
    }

    const uint8_t tone_index = static_cast<uint8_t>(chord.arp_tone_index % chord_tone_count_);
    startVoice(root_pitch + static_cast<float>(chord_tones_[tone_index]), 0.78f);
  }

  void triggerStep(uint32_t step_index)
  {
    const BassStep &bass = bass_steps_[step_index];
    const ChordStep &chord = chord_steps_[step_index];
    const bool has_bass = bass.root || bass.octave_up;
    const bool has_chord = chord.mode != kChordRest;

    if (!has_bass && !has_chord)
      return;

    if (has_bass)
      triggerBass(bass);
    if (has_chord)
      triggerChord(chord);
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

  float renderVoice(Voice &voice, float cutoff_base_hz, float resonance_q, float env_depth)
  {
    if (!voice.active)
      return 0.f;

    if (voice.releasing)
    {
      voice.env *= release_coeff_;
      voice.filter_env *= filter_release_coeff_;
    }
    else
    {
      if (voice.env < 1.f)
      {
        voice.env += attack_step_;
        if (voice.env > 1.f)
          voice.env = 1.f;
      }
      else
      {
        voice.env *= amp_decay_coeff_;
      }
      voice.filter_env *= filter_decay_coeff_;
    }

    if (!voice.releasing)
    {
      voice.gate_samples_remaining -= 1.f;
      if (voice.gate_samples_remaining <= 0.f)
        voice.releasing = true;
    }

    if (voice.env < kEnvelopeFloor)
    {
      voice.active = false;
      voice.env = 0.f;
      voice.filter_env = 0.f;
      voice.filter_state = 0.f;
      return 0.f;
    }

    voice.phase += voice.phase_inc;
    if (voice.phase >= 1.f)
      voice.phase -= 1.f;
    const float saw = 2.f * voice.phase - 1.f;

    const float filter_mod = 0.12f + env_depth * voice.filter_env;
    const float cutoff_hz = clipRange(cutoff_base_hz * filter_mod, 150.f, 14000.f);
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
    const float env_depth = 0.25f + env_mod_norm_ * 3.75f;

    float wet = 0.f;
    for (uint32_t voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
      wet += renderVoice(voices_[voiceIndex], cutoff_hz, resonance_q, env_depth);

    return wet;
  }

  BassStep bass_steps_[kStepsPerBar];
  ChordStep chord_steps_[kStepsPerBar];
  int8_t chord_tones_[kMaxChordTones];
  Voice voices_[kMaxVoices];
  uint32_t phrase_seed_ = 1U;
  uint32_t step_index_ = 0U;
  uint32_t next_voice_index_ = 0U;
  uint8_t chord_tone_count_ = 0U;
  float cutoff_norm_target_ = 0.58f;
  float resonance_norm_target_ = 0.42f;
  float cutoff_norm_smooth_ = 0.58f;
  float resonance_norm_smooth_ = 0.42f;
  float mix_ = 1.f;
  float decay_norm_ = 0.45f;
  float env_mod_norm_ = 0.65f;
  int8_t root_note_ = 36;
  float bpm_ = 120.f;
  float samples_per_tick_ = 6000.f;
  float samples_until_tick_ = 6000.f;
  float attack_step_ = 0.005f;
  float amp_decay_coeff_ = 0.9995f;
  float release_coeff_ = 0.998f;
  float filter_decay_coeff_ = 0.9995f;
  float filter_release_coeff_ = 0.9995f;
  bool running_ = false;
};
