#pragma once

/*
 * File: mohowl_engine.h
 *
 * Author-motif feedback howl for NTS-3. Same JP-8080 comb as FbOsc, gated
 * by the pad, with an attack pitch swoop and a little vibrato at high FEED.
 */

#define FBACKOSC_NO_OSC_API
#define FBACKOSC_MAX_DELAY 4096U
#include "fbackosc_engine.h"

#include <stdint.h>

class MoHowlEngine
{
public:
  static constexpr float kMinMidiNote = 36.f;
  static constexpr float kMaxMidiNote = 84.f;
  static constexpr float kAttackSec = 0.006f;
  static constexpr float kMinReleaseSec = 0.06f;
  static constexpr float kMaxReleaseSec = 0.9f;
  static constexpr float kSwoopSec = 0.2f;
  static constexpr float kMaxSwoopSemitones = 12.f;
  static constexpr float kVibratoHz = 5.2f;
  static constexpr float kVibratoSemitones = 0.18f;
  static constexpr float kOutputGain = 0.85f;

  struct Params
  {
    float pitch = 0.5f;
    float feedback = 0.62f;
    float harmonics = 0.5f;
    float swoop = 0.55f;
    float decay = 0.4f;
    float level = 0.7f;
  };

  void init()
  {
    params_ = Params();
    reset();
    applyParams();
  }

  void reset()
  {
    engine_.reset();
    env_ = 0.f;
    gated_ = false;
    swoop_env_ = 0.f;
    vibrato_phase_ = 0.f;
  }

  void setParams(const Params &params)
  {
    params_ = params;
    applyParams();
  }

  const Params &getParams() const { return params_; }

  void gate(bool on)
  {
    if (on && !gated_)
    {
      engine_.reset();
      engine_.randomizePhase();
      swoop_env_ = 1.f;
      vibrato_phase_ = 0.f;
    }
    gated_ = on;
  }

  float render(float sample_rate)
  {
    const float target_note = midiNote(params_.pitch);
    const float swoop_semitones = params_.swoop * kMaxSwoopSemitones;
    const float playing_note = target_note - swoop_semitones * swoop_env_;

    const float vibrato_depth = params_.feedback * kVibratoSemitones;
    const float vibrato = triangle(vibrato_phase_) * vibrato_depth;
    vibrato_phase_ += kVibratoHz / sample_rate;
    if (vibrato_phase_ >= 1.f)
      vibrato_phase_ -= 1.f;

    const float w0 = midiToW0(playing_note + vibrato, sample_rate);
    engine_.setPitch(w0, playing_note);

    const float attack = 1.f / (kAttackSec * sample_rate);
    const float release_sec = kMinReleaseSec + params_.decay * (kMaxReleaseSec - kMinReleaseSec);
    const float release = 1.f / (release_sec * sample_rate);
    const float swoop_step = 1.f / (kSwoopSec * sample_rate);

    if (gated_)
    {
      env_ += (1.f - env_) * attack;
      if (env_ > 1.f)
        env_ = 1.f;
    }
    else
    {
      env_ -= env_ * release;
      if (env_ < 1.0e-4f)
        env_ = 0.f;
    }

    swoop_env_ -= swoop_step;
    if (swoop_env_ < 0.f)
      swoop_env_ = 0.f;

    if (env_ <= 0.f)
      return 0.f;

    return engine_.render() * env_ * params_.level * kOutputGain;
  }

private:
  static float triangle(float phase)
  {
    if (phase < 0.5f)
      return 4.f * phase - 1.f;
    return 3.f - 4.f * phase;
  }

  static float midiNote(float pitch_0_1)
  {
    const float clamped = pitch_0_1 < 0.f ? 0.f : (pitch_0_1 > 1.f ? 1.f : pitch_0_1);
    return kMinMidiNote + clamped * (kMaxMidiNote - kMinMidiNote);
  }

  static float midiToW0(float note, float sample_rate)
  {
    float exponent = (note - 69.f) * (1.f / 12.f);
    float octave_scale = 1.f;
    while (exponent > 1.f)
    {
      exponent -= 1.f;
      octave_scale *= 2.f;
    }
    while (exponent < -1.f)
    {
      exponent += 1.f;
      octave_scale *= 0.5f;
    }
    const float x = exponent * 0.6931471805599453f;
    const float pow2 = 1.f + x * (1.f + x * (0.5f + x * (0.1666666667f + x * (0.0416666667f + x * 0.0083333333f))));
    return (440.f * octave_scale * pow2) / sample_rate;
  }

  void applyParams()
  {
    FBackOscEngine::Params osc;
    osc.harmonics = params_.harmonics;
    osc.feedback = params_.feedback;
    engine_.setParams(osc);
  }

  FBackOscEngine engine_;
  Params params_;
  float env_ = 0.f;
  float swoop_env_ = 0.f;
  float vibrato_phase_ = 0.f;
  bool gated_ = false;
};
