#pragma once

/*
 * File: revbass.h
 *
 * Hardstyle reverse bass. FREE swells while held; OFFB starts a half-beat
 * note on each off-beat crossing, with amplitude t^2 into the hit.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class RevBass : public Processor
{
public:
  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    PITCH = 0U,
    ATK,
    MIX,
    SYNC,
    NUM_PARAMS
  };

  enum
  {
    SYNC_FREE = 0,
    SYNC_OFFB
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case PITCH:
      pitch_norm_ = param_10bit_to_f32(value);
      break;
    case ATK:
      atk_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case SYNC:
      sync_ = (value != 0) ? SYNC_OFFB : SYNC_FREE;
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index != SYNC)
      return nullptr;
    return (value != 0) ? "OFFB" : "FREE";
  }

  void init(float *) override final
  {
    bpm_ = 120.f;
    clock_ = 0.f;
    note_pos_ = 0.f;
    note_len_ = 1.f;
    amp_ = 0.f;
    saw_phase_ = 0.f;
    pulse_phase_ = 0.f;
    note_on_ = false;
    pad_held_ = false;
  }

  void reset() override final
  {
    clock_ = 0.f;
    note_pos_ = 0.f;
    amp_ = 0.f;
    note_on_ = false;
  }

  void setTempo(float tempo) override final
  {
    if (tempo >= 40.f && tempo <= 300.f)
      bpm_ = tempo;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    const bool down = phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
                      phase == k_unit_touch_phase_stationary;
    if (down)
    {
      if (!pad_held_ && sync_ == SYNC_FREE)
        startNote(attackSamples());
      pad_held_ = true;
      return;
    }
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      pad_held_ = false;
      if (sync_ == SYNC_FREE)
        note_on_ = false;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    (void)raw;
    const float beat = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()));
    const float half = beat * 0.5f;
    const float midi = 30.f + pitch_norm_ * 50.f;
    const float saw_inc = fx::noteToInc(midi, getSampleRate());
    const float pulse_inc = fx::noteToInc(midi, getSampleRate());
    const float drive = 1.15f + atk_norm_ * 2.4f;
    const float rel_coeff = 1.f - fasterexpf(-1.f / 90.f);

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      const float prev_clock = clock_;
      clock_ += 1.f;
      if (clock_ >= beat)
        clock_ -= beat;

      if (sync_ == SYNC_OFFB && pad_held_)
      {
        const bool crossed = prev_clock < half && clock_ >= half;
        if (crossed)
          startNote(half);
      }

      if (note_on_)
      {
        note_pos_ += 1.f;
        const float t = fx::clip01(note_pos_ / note_len_);
        if (sync_ == SYNC_OFFB)
        {
          amp_ = t * t;
          if (note_pos_ >= note_len_)
          {
            amp_ = 0.f;
            note_on_ = false;
          }
        }
        else
        {
          amp_ = t * t;
          if (note_pos_ >= note_len_)
            amp_ = 1.f;
        }
      }
      else
      {
        amp_ += (0.f - amp_) * rel_coeff;
      }

      saw_phase_ = fx::wrap01(saw_phase_ + saw_inc);
      pulse_phase_ = fx::wrap01(pulse_phase_ + pulse_inc);
      const float saw = fx::blepSaw(saw_phase_, saw_inc);
      const float pulse = fx::blepPulse(pulse_phase_, pulse_inc, 0.48f);
      const float osc = saw * 0.78f + pulse * 0.22f;
      const float wet = fastertanhf(osc * amp_ * drive) * 0.7f;

      out[0] = fx::mix(in[0], wet, mix_);
      out[1] = fx::mix(in[1], wet * 0.96f, mix_);
      in += 2;
      out += 2;
    }
  }

private:
  float attackSamples() const
  {
    const float seconds = 0.02f + atk_norm_ * 0.18f;
    return seconds * getSampleRate();
  }

  void startNote(float length)
  {
    note_len_ = (length < 32.f) ? 32.f : length;
    note_pos_ = 0.f;
    note_on_ = true;
    amp_ = 0.f;
  }

  float clock_ = 0.f;
  float note_pos_ = 0.f;
  float note_len_ = 1.f;
  float amp_ = 0.f;
  float saw_phase_ = 0.f;
  float pulse_phase_ = 0.f;
  float bpm_ = 120.f;
  float pitch_norm_ = 0.35f;
  float atk_norm_ = 0.54f;
  float mix_ = 1.f;
  uint8_t sync_ = SYNC_OFFB;
  bool note_on_ = false;
  bool pad_held_ = false;
};
