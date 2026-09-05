#pragma once

/*
 * File: chordres.h
 *
 * Pad chords with a tempo-synced residue arpeggio after release.
 * Two residue layers can overlap.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class ChordRes : public Processor
{
public:
  static constexpr uint32_t kVoices = 4U;
  static constexpr uint32_t kLayers = 2U;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    PROG = 0U,
    VOIC,
    MIX,
    ROOT,
    TYPE,
    RES,
    DEC,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case PROG:
      prog_norm_ = param_10bit_to_f32(value);
      break;
    case VOIC:
      voic_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case ROOT:
      root_note_ = static_cast<int8_t>(fx::clip(static_cast<float>(value), 24.f, 48.f));
      break;
    case TYPE:
      major_ = value != 0;
      break;
    case RES:
      res_norm_ = param_10bit_to_f32(value);
      break;
    case DEC:
      decay_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index == TYPE)
      return (value != 0) ? "MAJ" : "MIN";
    if (index != ROOT)
      return nullptr;
    static char label[8];
    static const char *kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int32_t note = value;
    if (note < 0)
      note = 0;
    if (note > 127)
      note = 127;
    char *out = label;
    const char *name = kNames[note % 12];
    while (*name)
      *out++ = *name++;
    *out++ = static_cast<char>('0' + (note / 12) - 1);
    *out = '\0';
    return label;
  }

  void init(float *) override final
  {
    bpm_ = 120.f;
    pad_held_ = false;
    active_layer_ = 0U;
    for (uint32_t layerIndex = 0; layerIndex < kLayers; ++layerIndex)
      resetLayer(layers_[layerIndex]);
  }

  void reset() override final
  {
    for (uint32_t layerIndex = 0; layerIndex < kLayers; ++layerIndex)
      resetLayer(layers_[layerIndex]);
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 40.f && tempo < 300.f)
      bpm_ = tempo;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began)
    {
      Layer &layer = layers_[active_layer_];
      buildChord(layer);
      layer.held = true;
      layer.residue = false;
      layer.env = 1.f;
      layer.arp_index = 0U;
      layer.clock_acc = 0.f;
      pad_held_ = true;
      return;
    }
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      if (pad_held_)
      {
        layers_[active_layer_].held = false;
        layers_[active_layer_].residue = true;
        layers_[active_layer_].arp_index = 0U;
        layers_[active_layer_].clock_acc = 0.f;
        active_layer_ = (active_layer_ + 1U) % kLayers;
      }
      pad_held_ = false;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    (void)raw;
    const float sixteenth = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate())) * 0.25f;
    const float residue_steps = 8.f + res_norm_ * 24.f;
    const float decay = 0.9996f - (1.f - decay_norm_) * 0.0015f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float wet = 0.f;
      for (uint32_t layerIndex = 0; layerIndex < kLayers; ++layerIndex)
      {
        Layer &layer = layers_[layerIndex];
        if (layer.env <= 0.0001f && !layer.held)
          continue;

        if (layer.residue)
        {
          layer.clock_acc += 1.f;
          if (layer.clock_acc >= sixteenth)
          {
            layer.clock_acc -= sixteenth;
            if (layer.arp_index + 1U < layer.count)
              ++layer.arp_index;
            else
              layer.arp_index = 0U;
            layer.steps_left -= 1.f;
            if (layer.steps_left <= 0.f)
              layer.residue = false;
          }
        }

        const uint32_t voice_limit = layer.held ? layer.count : 1U;
        for (uint32_t voiceIndex = 0; voiceIndex < voice_limit; ++voiceIndex)
        {
          const uint32_t used = layer.held ? voiceIndex : layer.arp_index;
          const float inc = fx::noteToInc(layer.notes[used], getSampleRate());
          layer.phase[used] = fx::wrap01(layer.phase[used] + inc);
          wet += fx::blepSaw(layer.phase[used], inc) * layer.env * (layer.held ? 0.18f : 0.28f);
        }

        if (!layer.held)
          layer.env *= decay;
      }

      wet = fx::softclip(wet);
      out[0] = fx::mix(in[0], wet, mix_);
      out[1] = fx::mix(in[1], wet, mix_);
      in += 2;
      out += 2;
    }
    (void)residue_steps;
  }

private:
  struct Layer
  {
    float notes[kVoices];
    float phase[kVoices];
    uint32_t count;
    uint32_t arp_index;
    float env;
    float clock_acc;
    float steps_left;
    bool held;
    bool residue;
  };

  static void resetLayer(Layer &layer)
  {
    layer.count = 0U;
    layer.arp_index = 0U;
    layer.env = 0.f;
    layer.clock_acc = 0.f;
    layer.steps_left = 0.f;
    layer.held = false;
    layer.residue = false;
    for (uint32_t voiceIndex = 0; voiceIndex < kVoices; ++voiceIndex)
    {
      layer.notes[voiceIndex] = 60.f;
      layer.phase[voiceIndex] = 0.f;
    }
  }

  void buildChord(Layer &layer)
  {
    static const int8_t kMajProg[] = {0, 2, 4, 5, 7, 9, 11, 0};
    static const int8_t kMinProg[] = {0, 2, 3, 5, 7, 8, 10, 0};
    static const int8_t kMajThird[] = {0, 4, 7, 11};
    static const int8_t kMinThird[] = {0, 3, 7, 10};
    const float axis = prog_norm_ * 6.999f;
    const uint32_t degree = static_cast<uint32_t>(axis);
    const int8_t root_off = major_ ? kMajProg[degree] : kMinProg[degree];
    const bool chord_major = major_ ? (degree != 1 && degree != 2 && degree != 5) : (degree == 2 || degree == 5);
    const int8_t *third = chord_major ? kMajThird : kMinThird;
    const float spread = 0.f + voic_norm_ * 12.f;
    layer.count = 4U;
    layer.steps_left = 8.f + res_norm_ * 24.f;
    for (uint32_t voiceIndex = 0; voiceIndex < kVoices; ++voiceIndex)
    {
      const float extra = (voiceIndex >= 2U) ? spread * (static_cast<float>(voiceIndex) - 1.f) * 0.35f : 0.f;
      layer.notes[voiceIndex] = static_cast<float>(root_note_ + 12 + root_off + third[voiceIndex]) + extra;
      layer.phase[voiceIndex] = 0.f;
    }
    // Highest note first so residue descends.
    for (uint32_t a = 0; a < kVoices; ++a)
    {
      for (uint32_t b = a + 1U; b < kVoices; ++b)
      {
        if (layer.notes[a] < layer.notes[b])
        {
          const float tmp = layer.notes[a];
          layer.notes[a] = layer.notes[b];
          layer.notes[b] = tmp;
        }
      }
    }
  }

  Layer layers_[kLayers];
  float bpm_ = 120.f;
  float prog_norm_ = 0.f;
  float voic_norm_ = 0.4f;
  float res_norm_ = 0.63f;
  float decay_norm_ = 0.5f;
  float mix_ = 1.f;
  uint32_t active_layer_ = 0U;
  int8_t root_note_ = 36;
  bool major_ = false;
  bool pad_held_ = false;
};
