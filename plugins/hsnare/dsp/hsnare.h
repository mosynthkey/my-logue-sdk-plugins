#pragma once

/*
 * File: hsnare.h
 *
 * Tempo-synced 808/909 snare for NTS-3.
 * Hold the pad to run a 16-step phrase. X is hit density, Y morphs the
 * circuit from TR-808 (bridged-T sines + one HPF snappy) to TR-909
 * (triangle VCOs, 20 ms pitch bend, split LPF/HPF snappy).
 *
 * See RESEARCH.md. Independent of HClap; only the pad mapping is shared.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class HSnare : public Processor
{
public:
  static constexpr uint32_t kVoiceCount = 6U;
  static constexpr uint32_t kStepsPerBar = 16U;
  static constexpr float kVoiceGain = 0.42f;
  static constexpr float kDcCoeff = 0.99608f;
  static constexpr float kAnalogColorHz = 9000.f;
  static constexpr float k808LowHz = 173.3f;
  static constexpr float k808HighHz = 336.0f;
  static constexpr float k909LowHz = 190.0f;
  static constexpr float k909HighHz = 332.0f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    DENS = 0U,
    TYPE,
    MIX,
    TONE,
    SNAP,
    TUNE,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case DENS:
      dens_norm_ = param_10bit_to_f32(value);
      break;
    case TYPE:
      type_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case TONE:
      tone_norm_ = param_10bit_to_f32(value);
      break;
    case SNAP:
      snap_norm_ = param_10bit_to_f32(value);
      break;
    case TUNE:
      tune_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *) override final
  {
    dens_norm_ = 0.068f;
    type_norm_ = 0.f;
    tone_norm_ = 0.42f;
    snap_norm_ = 0.55f;
    tune_norm_ = 0.5f;
    mix_ = 1.f;
    bpm_ = 120.f;
    running_ = false;
    use_host_clock_ = false;
    tick_counter_ = 0U;
    internal_tick_phase_ = 0.f;
    flam_samples_left_ = 0;
    analog_state_ = 0xA5A5A5A5u;
    analog_lp_ = 0.f;
    lfsr_ = 0x7FFFFFFFu;
    dc_prev_in_ = 0.f;
    dc_prev_out_ = 0.f;
    trigger_count_ = 0U;
    analog_color_coeff_ = fx::onePoleCoeff(kAnalogColorHz, getSampleRate());
    resetVoices();
  }

  void reset() override final
  {
    running_ = false;
    flam_samples_left_ = 0;
    dc_prev_in_ = 0.f;
    dc_prev_out_ = 0.f;
    resetVoices();
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 20.f && tempo < 999.f)
      bpm_ = tempo;
  }

  void tempo4ppqnTick(uint32_t counter) override final
  {
    use_host_clock_ = true;
    handleTick(counter);
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
        phase == k_unit_touch_phase_stationary)
    {
      running_ = true;
      return;
    }

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      running_ = false;
      flam_samples_left_ = 0;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    const float dry_gain = 1.f - mix_;
    const float analog_coeff = analog_color_coeff_;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      if (!use_host_clock_)
        advanceInternalClockOneSample();
      advancePendingFlam();

      analog_lp_ += analog_coeff * (analogNoise() - analog_lp_);
      const float digital = lfsrNoise();
      const float noise = analog_lp_ + type_norm_ * (digital - analog_lp_);

      const float wet = renderVoices(noise) * mix_;
      out[0] = in[0] * dry_gain + wet;
      out[1] = in[1] * dry_gain + wet;
      in += 2;
      out += 2;
    }
  }

  void debugTrigger(float accent = 1.f) { triggerVoice(accent); }

  uint32_t debugTriggerCount() const { return trigger_count_; }

  uint32_t debugHits() const { return hitsFromDensity(dens_norm_); }

  bool debugStepHit(uint32_t step_index) const
  {
    return stepIsHit(step_index, hitsFromDensity(dens_norm_));
  }

private:
  struct Voice
  {
    bool active = false;
    float age = 0.f;
    float accent = 1.f;
    float low_phase = 0.f;
    float high_phase = 0.f;
    float low_env = 0.f;
    float high_env = 0.f;
    float snap_env = 0.f;
    float snap_lp_env = 0.f;
    float snap_hp_z = 0.f;
    float snap_lp_z = 0.f;
  };

  static uint32_t hitsFromDensity(float dens_norm)
  {
    uint32_t hits = 1U + static_cast<uint32_t>(dens_norm * 15.f + 0.5f);
    if (hits < 1U)
      hits = 1U;
    if (hits > 16U)
      hits = 16U;
    return hits;
  }

  static bool stepIsHit(uint32_t step_index, uint32_t hits)
  {
    return fx::euclidHit((step_index + 12U) % kStepsPerBar, hits, kStepsPerBar);
  }

  static bool isBackbeat(uint32_t step_index)
  {
    return step_index == 4U || step_index == 12U;
  }

  static float triangle(float phase)
  {
    const float folded = 4.f * phase;
    if (folded < 2.f)
      return folded - 1.f;
    return 3.f - folded;
  }

  // Per-sample multiply coeffs sit near 1 (e.g. exp(-1/(0.085*48000)) ≈ 0.99975).
  // fasterexpf is biased around 0 (fasterexpf(0) ≈ 0.971), so using it here collapses
  // an ~85 ms body to ~5 ms — attack click only on device. Linearize for tiny |x|.
  static float envCoeff(float seconds, float sample_rate)
  {
    const float clamped = fx::clip(seconds, 0.008f, 0.8f);
    const float rate = (sample_rate > 1.f) ? sample_rate : 48000.f;
    const float x = -1.f / (clamped * rate);
    return fx::clip(1.f + x, 0.f, 1.f);
  }

  float analogNoise()
  {
    analog_state_ = analog_state_ * 1664525U + 1013904223U;
    return (static_cast<float>(analog_state_) * (1.f / 2147483648.f)) - 1.f;
  }

  float lfsrNoise()
  {
    const uint32_t bit = ((lfsr_ >> 30) ^ (lfsr_ >> 12)) & 1U;
    lfsr_ = ((lfsr_ << 1) | bit) & 0x7FFFFFFFu;
    if (lfsr_ == 0U)
      lfsr_ = 0x7FFFFFFFu;
    return (lfsr_ & 1U) ? 1.f : -1.f;
  }

  void resetVoices()
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
      voices_[voiceIndex] = Voice{};
    next_voice_index_ = 0U;
  }

  void handleTick(uint32_t counter)
  {
    tick_counter_ = counter;
    if (!running_)
      return;

    const uint32_t step_index = (counter - 1U) % kStepsPerBar;
    const uint32_t hits = hitsFromDensity(dens_norm_);
    if (!stepIsHit(step_index, hits))
      return;

    triggerVoice(1.f);
    if (dens_norm_ > 0.82f && isBackbeat(step_index) && bpm_ > 0.f)
    {
      const float samples_per_16th = getSampleRate() * 60.f / (bpm_ * 4.f);
      flam_samples_left_ = static_cast<int32_t>(samples_per_16th * 0.5f);
    }
  }

  void advanceInternalClockOneSample()
  {
    if (bpm_ <= 0.f)
      return;

    const float samples_per_tick = getSampleRate() * 60.f / (bpm_ * 4.f);
    if (samples_per_tick <= 0.f)
      return;

    internal_tick_phase_ += 1.f;
    if (internal_tick_phase_ >= samples_per_tick)
    {
      internal_tick_phase_ -= samples_per_tick;
      ++tick_counter_;
      handleTick(tick_counter_);
    }
  }

  void advancePendingFlam()
  {
    if (flam_samples_left_ <= 0)
      return;
    --flam_samples_left_;
    if (flam_samples_left_ == 0)
      triggerVoice(0.72f);
  }

  void triggerVoice(float accent)
  {
    Voice &voice = voices_[next_voice_index_];
    next_voice_index_ = (next_voice_index_ + 1U) % kVoiceCount;
    voice.active = true;
    voice.age = 0.f;
    voice.accent = accent;
    voice.low_phase = 0.f;
    voice.high_phase = 0.f;
    voice.low_env = 1.f;
    voice.high_env = 1.f;
    voice.snap_env = 1.f;
    voice.snap_lp_env = 1.f;
    ++trigger_count_;
  }

  float renderVoice(Voice &voice, float noise, float low_hz, float high_hz, float type_norm, float tone,
                    float snap, float low_decay, float high_decay, float snap_decay, float snap_lp_decay,
                    float hp_coeff, float lp_coeff)
  {
    const float bend = type_norm * 0.55f * fasterexpf(-voice.age / 0.012f);
    const float low_inc = (low_hz * (1.f + bend)) / getSampleRate();
    const float high_inc = (high_hz * (1.f + bend)) / getSampleRate();

    const float sine_low = fastersinfullf(voice.low_phase * 6.283185307179586f);
    const float sine_high = fastersinfullf(voice.high_phase * 6.283185307179586f);
    const float tri_low = fastertanhf(triangle(voice.low_phase) * 1.35f);
    const float tri_high = fastertanhf(triangle(voice.high_phase) * 1.35f);

    const float low_wave = sine_low + type_norm * (tri_low - sine_low);
    const float high_wave = sine_high + type_norm * (tri_high - sine_high);
    const float shell = ((1.f - tone) * low_wave * voice.low_env + tone * high_wave * voice.high_env) *
                        (0.95f + 0.2f * snap);

    voice.snap_hp_z += hp_coeff * (noise - voice.snap_hp_z);
    const float hp_noise = noise - voice.snap_hp_z;
    voice.snap_lp_z += lp_coeff * (noise - voice.snap_lp_z);

    const float snap_808 = hp_noise * voice.snap_env;
    const float snap_909 = voice.snap_lp_z * voice.snap_lp_env * 0.55f + hp_noise * voice.snap_env;
    const float snappy = (snap_808 + type_norm * (snap_909 - snap_808)) * snap * 1.15f;

    voice.low_phase = fx::wrap01(voice.low_phase + low_inc);
    voice.high_phase = fx::wrap01(voice.high_phase + high_inc);
    voice.low_env *= low_decay;
    voice.high_env *= high_decay;
    voice.snap_env *= snap_decay;
    voice.snap_lp_env *= snap_lp_decay;
    voice.age += 1.f / getSampleRate();

    if (voice.low_env < 0.0008f && voice.high_env < 0.0008f && voice.snap_env < 0.0008f &&
        voice.snap_lp_env < 0.0008f)
      voice.active = false;

    const float type_gain = 1.f - type_norm * 0.48f;
    return (shell * voice.accent + snappy) * kVoiceGain * type_gain;
  }

  float renderVoices(float noise)
  {
    const float type_norm = type_norm_;
    const float tone = fx::clip01(tone_norm_);
    const float snap = fx::clip01(snap_norm_);
    const float tune = fasterpow2f((tune_norm_ * 2.f - 1.f) * 0.7f);

    const float low_hz = (k808LowHz + type_norm * (k909LowHz - k808LowHz)) * tune;
    const float high_hz = (k808HighHz + type_norm * (k909HighHz - k808HighHz)) * tune;

    const float sample_rate = getSampleRate();
    const float low_decay = envCoeff(0.085f + type_norm * 0.07f, sample_rate);
    const float high_decay = envCoeff(0.070f + type_norm * 0.045f, sample_rate);
    const float snap_decay = envCoeff(0.075f - type_norm * 0.02f, sample_rate);
    const float snap_lp_decay = envCoeff(0.16f + type_norm * 0.04f, sample_rate);
    const float hp_coeff = fx::onePoleCoeff(1600.f + tone * 900.f + type_norm * 400.f, getSampleRate());
    const float lp_coeff = fx::onePoleCoeff(2800.f - type_norm * 400.f, getSampleRate());

    float sum = 0.f;
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
    {
      Voice &voice = voices_[voiceIndex];
      if (!voice.active)
        continue;
      sum += renderVoice(voice, noise, low_hz, high_hz, type_norm, tone, snap, low_decay, high_decay,
                         snap_decay, snap_lp_decay, hp_coeff, lp_coeff);
    }

    const float blocked = sum - dc_prev_in_ + kDcCoeff * dc_prev_out_;
    dc_prev_in_ = sum;
    dc_prev_out_ = blocked;
    return fx::softclip(blocked);
  }

  Voice voices_[kVoiceCount];
  uint32_t next_voice_index_ = 0U;
  uint32_t tick_counter_ = 0U;
  uint32_t analog_state_ = 1U;
  uint32_t lfsr_ = 0x7FFFFFFFu;
  uint32_t trigger_count_ = 0U;
  int32_t flam_samples_left_ = 0;
  float dens_norm_ = 0.f;
  float type_norm_ = 0.f;
  float tone_norm_ = 0.42f;
  float snap_norm_ = 0.55f;
  float tune_norm_ = 0.5f;
  float mix_ = 1.f;
  float bpm_ = 120.f;
  float internal_tick_phase_ = 0.f;
  float analog_lp_ = 0.f;
  float analog_color_coeff_ = 0.7f;
  float dc_prev_in_ = 0.f;
  float dc_prev_out_ = 0.f;
  bool running_ = false;
  bool use_host_clock_ = false;
};
