#pragma once

/*
 * File: shaker.h
 *
 * PhISEM shaker (Cook, 1997). Instrument constants follow STK Shakers
 * (Perry R. Cook / Gary Scavone); this is not a copy of STK source.
 * Used as osc on NTS-1 mkII and as genericfx on NTS-3.
 *
 */

#include "processor.h"
#include "macros.h"
#include "touch_phase.h"
#include <math.h>
#include <stdint.h>

class Shaker : public Processor
{
public:
  static constexpr uint32_t kMaxResonators = 8;
  static constexpr float kMinEnergy = 0.001f;
  static constexpr float kMaxShake = 1.f;
  static constexpr float kTwoPi = 6.283185307179586f;
  static constexpr float kRefSampleRate = 44100.f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    TYPE = 0U,
    DENS,
    DECAY,
    MIX,
    NUM_PARAMS
  };

  enum
  {
    PRESET_MARACA = 0,
    PRESET_CABASA,
    PRESET_SEKERE,
    PRESET_TAMBRN,
    PRESET_SLEIGH,
    PRESET_BAMBOO,
    PRESET_SAND,
    PRESET_CAN,
    PRESET_ROCKS,
    PRESET_GRAVEL,
    PRESET_ANGKLU,
    NUM_PRESETS
  };

  void setStereoMix(bool enabled) { stereo_mix_ = enabled; }

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case TYPE:
    {
      uint32_t preset = static_cast<uint32_t>(value);
      if (preset >= NUM_PRESETS)
        preset = NUM_PRESETS - 1;
      if (preset != preset_index_)
        applyPreset(preset);
      break;
    }
    case DENS:
      dens_norm_ = param_10bit_to_f32(value);
      applyDensity();
      break;
    case DECAY:
      decay_norm_ = param_10bit_to_f32(value);
      applyDecay();
      break;
    case MIX:
      mix_ = value / 1000.f;
      if (mix_ < 0.f)
        mix_ = 0.f;
      if (mix_ > 1.f)
        mix_ = 1.f;
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    static const char *preset_names[NUM_PRESETS] = {
        "MARACA",
        "CABASA",
        "SEKERE",
        "TAMBRN",
        "SLEIGH",
        "BAMBOO",
        "SAND",
        "CAN",
        "ROCKS",
        "GRAVEL",
        "ANGKLU",
    };

    if (index == TYPE && value >= PRESET_MARACA && value < NUM_PRESETS)
      return preset_names[value];

    return nullptr;
  }

  void init(float *) override final
  {
    rng_ = 0xA5A5A5A5u;
    dens_norm_ = 0.5f;
    decay_norm_ = 0.5f;
    mix_ = 1.f;
    freq_scale_ = 1.f;
    applyPreset(PRESET_MARACA);
    clearState();
  }

  void reset() override final { clearState(); }

  void setPitch(float w0)
  {
    const float hz = w0 * getSampleRate();
    float scale = hz / 261.625565f;
    if (scale < 0.25f)
      scale = 0.25f;
    if (scale > 4.f)
      scale = 4.f;
    freq_scale_ = scale;
    updatePoles();
  }

  void noteOn(uint8_t note, uint8_t velo) override final
  {
    float scale = powf(2.f, (static_cast<int>(note) - 60) * (1.f / 12.f));
    if (scale < 0.25f)
      scale = 0.25f;
    if (scale > 4.f)
      scale = 4.f;
    freq_scale_ = scale;
    updatePoles();

    const float amount = (velo * (1.f / 127.f)) * 0.2f + 0.05f;
    shake(amount);
  }

  void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    (void)id;
    const float px = static_cast<float>(x);
    const float py = static_cast<float>(y);

    if (phase == kLogueTouchBegan)
    {
      shake(0.2f);
      last_x_ = px;
      last_y_ = py;
      touching_ = true;
      return;
    }

    if ((phase == kLogueTouchMoved || phase == kLogueTouchStationary) && touching_)
    {
      const float dx = px - last_x_;
      const float dy = py - last_y_;
      last_x_ = px;
      last_y_ = py;
      const float dist = sqrtf(dx * dx + dy * dy);
      float norm = dist * (1.f / 120.f);
      if (norm > 1.f)
        norm = 1.f;
      const float energy = powf(norm, 1.8f) * 0.12f;
      if (energy > kMinEnergy)
        shake(energy);
      return;
    }

    if (phase == kLogueTouchEnded || phase == kLogueTouchCancelled)
      touching_ = false;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    // NTS-3 always feeds the main oscillator (often a saw) into FX slots.
    // Treat Shaker as an instrument and never pass that dry signal through.
    (void)in;
    const float wet_amt = stereo_mix_ ? mix_ : 1.f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      const float wet = tick() * wet_amt;
      if (stereo_mix_)
      {
        out[0] = wet;
        out[1] = wet;
        out += 2;
      }
      else
      {
        out[0] = wet;
        ++out;
      }
    }
  }

private:
  struct Preset
  {
    float gain;
    float objects;
    float sound_decay;
    float system_decay;
    float vary;
    uint8_t n_res;
    uint8_t tuned;
    float freqs[kMaxResonators];
    float radii[kMaxResonators];
    float gains[kMaxResonators];
    uint8_t do_vary[kMaxResonators];
    float eq[3];
  };

  struct Resonator
  {
    float freq;
    float radius;
    float gain;
    uint8_t do_vary;
    float a1;
    float a2;
    float y1;
    float y2;
  };

  static const Preset &presetAt(uint32_t index)
  {
    static const Preset kPresets[NUM_PRESETS] = {
        {4.f, 25.f, 0.95f, 0.999f, 0.f, 1, 0, {3200.f}, {0.96f}, {1.f}, {0}, {1.f, -1.f, 0.f}},
        {8.f, 512.f, 0.96f, 0.997f, 0.f, 1, 0, {3000.f}, {0.7f}, {1.f}, {0}, {1.f, -1.f, 0.f}},
        {4.f, 64.f, 0.96f, 0.999f, 0.f, 1, 0, {5500.f}, {0.6f}, {1.f}, {0}, {1.f, 0.f, -1.f}},
        {1.f, 32.f, 0.95f, 0.9985f, 0.05f, 3, 0, {2300.f, 5600.f, 8100.f}, {0.96f, 0.99f, 0.99f}, {0.1f, 0.8f, 1.f}, {0, 1, 1}, {1.f, 0.f, -1.f}},
        {1.f, 32.f, 0.97f, 0.9994f, 0.03f, 5, 0, {2500.f, 5300.f, 6500.f, 8300.f, 9800.f}, {0.99f, 0.99f, 0.99f, 0.99f, 0.99f}, {1.f, 1.f, 1.f, 0.5f, 0.3f}, {1, 1, 1, 1, 1}, {1.f, 0.f, -1.f}},
        {0.4f, 1.2f, 0.9f, 0.9999f, 0.2f, 3, 0, {2800.f, 2240.f, 3360.f}, {0.995f, 0.995f, 0.995f}, {1.f, 1.f, 1.f}, {1, 1, 1}, {1.f, 0.f, 0.f}},
        {0.5f, 128.f, 0.999f, 0.999f, 0.f, 1, 0, {4500.f}, {0.6f}, {1.f}, {0}, {1.f, 0.f, -1.f}},
        {0.5f, 48.f, 0.97f, 0.999f, 0.f, 5, 0, {370.f, 1025.f, 1424.f, 2149.f, 3596.f}, {0.99f, 0.992f, 0.992f, 0.992f, 0.992f}, {1.f, 1.8f, 1.8f, 1.8f, 1.8f}, {0, 0, 0, 0, 0}, {1.f, 0.f, -1.f}},
        {4.f, 23.f, 0.98f, 0.9965f, 0.11f, 1, 0, {6460.f}, {0.932f}, {1.f}, {1}, {1.f, 0.f, -1.f}},
        {4.f, 1600.f, 0.98f, 0.99586f, 0.18f, 1, 0, {9000.f}, {0.843f}, {1.f}, {1}, {1.f, 0.f, -1.f}},
        {0.5f, 1.2f, 0.95f, 0.9999f, 0.f, 7, 1, {1046.6f, 1174.8f, 1397.f, 1568.f, 1760.f, 2093.3f, 2350.f}, {0.996f, 0.996f, 0.996f, 0.996f, 0.996f, 0.996f, 0.996f}, {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f}, {0, 0, 0, 0, 0, 0, 0}, {1.f, 0.f, -1.f}},
    };
    return kPresets[index];
  }

  static float correctedDecay(float decay_44k)
  {
    return powf(decay_44k, kRefSampleRate / getSampleRate());
  }

  void shake(float amount)
  {
    shake_energy_ += amount;
    if (shake_energy_ > kMaxShake)
      shake_energy_ = kMaxShake;
  }

  float rand01()
  {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (rng_ & 0xFFFFFFu) * (1.f / 16777216.f);
  }

  float poleFor(float freq, float radius) const
  {
    float hz = freq * freq_scale_;
    const float nyquist_limit = 0.45f * getSampleRate();
    if (hz > nyquist_limit)
      hz = nyquist_limit;
    return -2.f * radius * cosf(kTwoPi * hz / getSampleRate());
  }

  void updatePoles()
  {
    for (uint32_t resonatorIndex = 0; resonatorIndex < n_res_; ++resonatorIndex)
    {
      Resonator &resonator = resonators_[resonatorIndex];
      resonator.a1 = poleFor(resonator.freq, resonator.radius);
    }
  }

  void applyDensity()
  {
    const float scale = 0.25f * powf(16.f, dens_norm_);
    objects_ = preset_objects_ * scale;
    if (objects_ < 1.1f)
      objects_ = 1.1f;
    threshold_ = objects_ * (kRefSampleRate / getSampleRate());
    current_gain_ = logf(objects_) * base_gain_ / objects_;
  }

  void applyDecay()
  {
    float system_44k;
    if (decay_norm_ < 0.5f)
    {
      const float blend = decay_norm_ * 2.f;
      system_44k = 0.99f + (preset_system_decay_ - 0.99f) * blend;
    }
    else
    {
      const float blend = (decay_norm_ - 0.5f) * 2.f;
      system_44k = preset_system_decay_ + (0.99995f - preset_system_decay_) * blend;
    }
    system_decay_ = correctedDecay(system_44k);
  }

  void applyPreset(uint32_t index)
  {
    const Preset &preset = presetAt(index);
    preset_index_ = index;
    base_gain_ = preset.gain;
    preset_objects_ = preset.objects;
    preset_system_decay_ = preset.system_decay;
    sound_decay_ = correctedDecay(preset.sound_decay);
    vary_ = preset.vary;
    tuned_ = preset.tuned != 0;
    n_res_ = preset.n_res;
    if (n_res_ > kMaxResonators)
      n_res_ = kMaxResonators;

    for (uint32_t resonatorIndex = 0; resonatorIndex < n_res_; ++resonatorIndex)
    {
      Resonator &resonator = resonators_[resonatorIndex];
      resonator.freq = preset.freqs[resonatorIndex];
      resonator.radius = preset.radii[resonatorIndex];
      resonator.gain = preset.gains[resonatorIndex];
      resonator.do_vary = preset.do_vary[resonatorIndex];
      resonator.a2 = resonator.radius * resonator.radius;
      resonator.y1 = 0.f;
      resonator.y2 = 0.f;
    }

    eq_b0_ = preset.eq[0];
    eq_b1_ = preset.eq[1];
    eq_b2_ = preset.eq[2];
    eq_x1_ = 0.f;
    eq_x2_ = 0.f;
    snd_level_ = 0.f;
    tube_index_ = 0;

    applyDensity();
    applyDecay();
    updatePoles();
  }

  void clearState()
  {
    shake_energy_ = 0.f;
    snd_level_ = 0.f;
    eq_x1_ = 0.f;
    eq_x2_ = 0.f;
    touching_ = false;
    for (uint32_t resonatorIndex = 0; resonatorIndex < kMaxResonators; ++resonatorIndex)
    {
      resonators_[resonatorIndex].y1 = 0.f;
      resonators_[resonatorIndex].y2 = 0.f;
    }
  }

  float tick()
  {
    float input = 0.f;

    if (shake_energy_ >= kMinEnergy)
    {
      shake_energy_ *= system_decay_;

      if (rand01() * 1024.f < threshold_)
      {
        snd_level_ += shake_energy_;
        input = snd_level_;

        if (vary_ > 0.f)
        {
          for (uint32_t resonatorIndex = 0; resonatorIndex < n_res_; ++resonatorIndex)
          {
            Resonator &resonator = resonators_[resonatorIndex];
            if (!resonator.do_vary)
              continue;
            const float freq = resonator.freq * (1.f + vary_ * (2.f * rand01() - 1.f));
            resonator.a1 = poleFor(freq, resonator.radius);
          }
        }

        if (tuned_)
        {
          uint32_t pick = static_cast<uint32_t>(rand01() * static_cast<float>(n_res_));
          if (pick >= n_res_)
            pick = n_res_ - 1;
          tube_index_ = pick;
        }
      }
    }

    snd_level_ *= sound_decay_;

    float sum = 0.f;
    for (uint32_t resonatorIndex = 0; resonatorIndex < n_res_; ++resonatorIndex)
    {
      Resonator &resonator = resonators_[resonatorIndex];
      const float drive = (tuned_ && resonatorIndex != tube_index_) ? 0.f : input;
      float y = drive * resonator.gain * current_gain_ - resonator.a1 * resonator.y1 - resonator.a2 * resonator.y2;
      if (y > 8.f)
        y = 8.f;
      else if (y < -8.f)
        y = -8.f;
      resonator.y2 = resonator.y1;
      resonator.y1 = y;
      sum += y;
    }

    const float shaped = eq_b0_ * sum + eq_b1_ * eq_x1_ + eq_b2_ * eq_x2_;
    eq_x2_ = eq_x1_;
    eq_x1_ = sum;
    return tanhf(shaped * 0.5f);
  }

  bool stereo_mix_ = false;
  bool touching_ = false;
  bool tuned_ = false;
  uint32_t preset_index_ = 0;
  uint32_t n_res_ = 1;
  uint32_t tube_index_ = 0;
  uint32_t rng_ = 0xA5A5A5A5u;
  float dens_norm_ = 0.5f;
  float decay_norm_ = 0.5f;
  float mix_ = 1.f;
  float freq_scale_ = 1.f;
  float base_gain_ = 4.f;
  float preset_objects_ = 25.f;
  float preset_system_decay_ = 0.999f;
  float objects_ = 25.f;
  float threshold_ = 25.f;
  float current_gain_ = 1.f;
  float sound_decay_ = 0.95f;
  float system_decay_ = 0.999f;
  float vary_ = 0.f;
  float shake_energy_ = 0.f;
  float snd_level_ = 0.f;
  float eq_b0_ = 1.f;
  float eq_b1_ = -1.f;
  float eq_b2_ = 0.f;
  float eq_x1_ = 0.f;
  float eq_x2_ = 0.f;
  float last_x_ = 0.f;
  float last_y_ = 0.f;
  Resonator resonators_[kMaxResonators];
};
