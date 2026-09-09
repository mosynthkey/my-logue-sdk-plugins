#pragma once

/*
 * File: hhat.h
 *
 * Tempo-synced TR-909 hi-hat for NTS-3.
 * Hold the pad to run a 16-step phrase. X is Euclidean hit density
 * (step-synced). Y is a continuous Close → Open continuum.
 *
 * Voice path models the 909 hi-hat PCM board (not 808 metal squares):
 *   30 kHz ZOH clock → shared CH/OH ROM → 6-bit resistor DAC
 *   → analog decay VCA → reconstruction LPFs
 * Closed = ROM 0x6000..0x7FFF, Open = 0x0000..0x5FFF (Fraser / Network-909).
 * Y crossfades those windows and morphs the decay τ. See RESEARCH.md.
 */

#include "fx_dsp.h"
#include "hhat_pcm.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "tr909_pcm.h"
#include "utils/float_math.h"
#include <stdint.h>

class HHat : public Processor
{
public:
  static constexpr uint32_t kVoiceCount = 6U;
  static constexpr uint32_t kStepsPerBar = 16U;
  static constexpr uint32_t kClosedStart = 0x6000U;
  static constexpr uint32_t kClosedEnd = 0x8000U;
  static constexpr uint32_t kOpenStart = 0x0000U;
  static constexpr uint32_t kOpenEnd = 0x6000U;
  static constexpr float kVoiceGain = 0.48f;
  static constexpr float kRomPhaseInc = tr909::kRomPhaseInc;
  static constexpr float kLpfACoeff = tr909::kLpfACoeff;
  static constexpr float kLpfBCoeff = tr909::kLpfBCoeff;
  static constexpr float kDcCoeff = tr909::kDcCoeff;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    DENS = 0U,
    OPEN,
    MIX,
    TONE,
    TUNE,
    DEC,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case DENS:
      dens_norm_ = param_10bit_to_f32(value);
      break;
    case OPEN:
      open_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case TONE:
      tone_norm_ = param_10bit_to_f32(value);
      break;
    case TUNE:
      tune_norm_ = param_10bit_to_f32(value);
      updateClockRatio();
      break;
    case DEC:
      decay_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final { return nullptr; }

  void init(float *) override final
  {
    dens_norm_ = 0.47f;
    open_norm_ = 0.f;
    tone_norm_ = 0.55f;
    tune_norm_ = 0.5f;
    decay_norm_ = 0.5f;
    mix_ = 1.f;
    bpm_ = 120.f;
    running_ = false;
    use_host_clock_ = false;
    tick_counter_ = 0U;
    internal_tick_phase_ = 0.f;
    roll_samples_left_ = 0;
    roll_hits_left_ = 0;
    dc_prev_in_ = 0.f;
    dc_prev_out_ = 0.f;
    trigger_count_ = 0U;
    updateClockRatio();
    resetVoices();
  }

  void reset() override final
  {
    running_ = false;
    roll_samples_left_ = 0;
    roll_hits_left_ = 0;
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
      roll_samples_left_ = 0;
      roll_hits_left_ = 0;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    const float inv_sr = 1.f / getSampleRate();
    // TONE tilts the first reconstruction pole (darker ↔ brighter).
    const float lpf_a_coeff = fx::clip(kLpfACoeff - 0.18f + tone_norm_ * 0.36f, 0.28f, 0.82f);

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      if (!use_host_clock_)
        advanceInternalClockOneSample();
      advancePendingRoll();

      const float wet = renderVoices(lpf_a_coeff, inv_sr) * mix_;
      out[0] = in[0] + wet;
      out[1] = in[1] + wet;
      in += 2;
      out += 2;
    }
  }

  void debugTrigger(float accent = 1.f) { triggerVoice(accent, open_norm_); }

  void debugSetOpen(float open_norm) { open_norm_ = fx::clip01(open_norm); }

  uint32_t debugTriggerCount() const { return trigger_count_; }

  uint32_t debugHits() const { return hitsFromDensity(dens_norm_); }

  bool debugStepHit(uint32_t step_index) const
  {
    return stepIsHit(step_index, hitsFromDensity(dens_norm_));
  }

  float debugTauSeconds() const { return tauFromOpen(open_norm_, 0.55f + decay_norm_ * 1.1f); }

private:
  struct Voice
  {
    bool active = false;
    float age = 0.f;
    float accent = 1.f;
    float open_amount = 0.f;
    float tau = 0.05f;
    float phase_ch = 0.f;
    float phase_oh = 0.f;
    float phase_inc = kRomPhaseInc;
    float lpf_a = 0.f;
    float lpf_b = 0.f;
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
    return fx::euclidHit(step_index % kStepsPerBar, hits, kStepsPerBar);
  }

  static bool isDownbeatSixteenth(uint32_t step_index)
  {
    return (step_index % 4U) == 0U;
  }

  static float tauFromOpen(float open_norm, float decay_scale)
  {
    // Hardware CH is short; OH Decay pot spans much longer.
    const float closed_tau = 0.038f;
    const float open_tau = 0.34f;
    return (closed_tau + open_norm * (open_tau - closed_tau)) * decay_scale;
  }

  static float readWindow(float phase, uint32_t start, uint32_t end)
  {
    const uint32_t sample_index = start + static_cast<uint32_t>(phase);
    if (sample_index >= end || sample_index >= kTr909HhPcmLength)
      return 0.f;
    return tr909::dacFromPacked(kTr909HhPcmPacked, sample_index);
  }

  void updateClockRatio()
  {
    // TUNE ≈ ±7 st around the fixed 30 kHz hat clock (hardware has no Tune).
    const float semis = (tune_norm_ * 2.f - 1.f) * 7.f;
    clock_ratio_ = tr909::exp2Approx(semis * (1.f / 12.f));
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

    const float accent = isDownbeatSixteenth(step_index) ? 1.f : 0.82f;
    triggerVoice(accent, open_norm_);

    if (dens_norm_ > 0.84f && bpm_ > 0.f && (step_index % 2U) == 0U)
    {
      const float samples_per_16th = getSampleRate() * 60.f / (bpm_ * 4.f);
      roll_samples_left_ = static_cast<int32_t>(samples_per_16th * 0.5f);
      roll_hits_left_ = 1 + static_cast<int32_t>((dens_norm_ - 0.84f) * 8.f);
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

  void advancePendingRoll()
  {
    if (roll_hits_left_ <= 0 || roll_samples_left_ <= 0)
      return;
    --roll_samples_left_;
    if (roll_samples_left_ > 0)
      return;

    // Rolls stay closed-ish so the open body does not smear.
    triggerVoice(0.62f, open_norm_ * 0.25f);
    --roll_hits_left_;
    if (roll_hits_left_ > 0 && bpm_ > 0.f)
    {
      const float samples_per_16th = getSampleRate() * 60.f / (bpm_ * 4.f);
      roll_samples_left_ = static_cast<int32_t>(samples_per_16th * 0.5f);
    }
  }

  void chokeOpenVoices(float new_open)
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
    {
      Voice &voice = voices_[voiceIndex];
      if (!voice.active)
        continue;
      if (voice.open_amount > new_open + 0.08f)
      {
        voice.tau *= 0.18f;
        if (voice.tau < 0.010f)
          voice.tau = 0.010f;
        if (voice.age < voice.tau * 0.4f)
          voice.age = voice.tau * 0.4f;
      }
    }
  }

  void triggerVoice(float accent, float open_amount)
  {
    const float open = fx::clip01(open_amount);
    chokeOpenVoices(open);

    Voice &voice = voices_[next_voice_index_];
    next_voice_index_ = (next_voice_index_ + 1U) % kVoiceCount;
    voice.active = true;
    voice.age = 0.f;
    voice.accent = accent;
    voice.open_amount = open;
    voice.tau = tauFromOpen(open, 0.55f + decay_norm_ * 1.1f);
    voice.phase_ch = 0.f;
    voice.phase_oh = 0.f;
    voice.phase_inc = kRomPhaseInc * clock_ratio_;
    voice.lpf_a = 0.f;
    voice.lpf_b = 0.f;
    ++trigger_count_;
  }

  float renderVoice(Voice &voice, float lpf_a_coeff, float inv_sr)
  {
    const float pedal = open_norm_;
    if (pedal + 0.12f < voice.open_amount)
    {
      voice.tau *= 0.9965f;
      if (voice.tau < 0.010f)
        voice.tau = 0.010f;
    }

    const float open = voice.open_amount;
    const float ch = readWindow(voice.phase_ch, kClosedStart, kClosedEnd);
    const float oh = readWindow(voice.phase_oh, kOpenStart, kOpenEnd);
    // Equal-power-ish crossfade so half-open keeps body.
    const float oh_w = open * open;
    const float ch_w = 1.f - oh_w;
    const float dac = ch * ch_w + oh * oh_w;

    const float amp = fasterexpf(-voice.age / voice.tau) * voice.accent;
    const float vca = dac * amp * kVoiceGain;

    voice.lpf_a += lpf_a_coeff * (vca - voice.lpf_a);
    voice.lpf_b += kLpfBCoeff * (voice.lpf_a - voice.lpf_b);

    voice.phase_ch += voice.phase_inc;
    voice.phase_oh += voice.phase_inc;
    voice.age += inv_sr;

    const float ch_len = static_cast<float>(kClosedEnd - kClosedStart);
    const float oh_len = static_cast<float>(kOpenEnd - kOpenStart);
    if (voice.age > voice.tau * 8.f || amp < 0.001f ||
        (voice.phase_ch >= ch_len && voice.phase_oh >= oh_len))
      voice.active = false;

    return voice.lpf_b;
  }

  float renderVoices(float lpf_a_coeff, float inv_sr)
  {
    float sum = 0.f;
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
    {
      Voice &voice = voices_[voiceIndex];
      if (!voice.active)
        continue;
      sum += renderVoice(voice, lpf_a_coeff, inv_sr);
    }

    return tr909::dcBlock(sum, dc_prev_in_, dc_prev_out_);
  }

  Voice voices_[kVoiceCount];
  uint32_t next_voice_index_ = 0U;
  uint32_t tick_counter_ = 0U;
  uint32_t trigger_count_ = 0U;
  int32_t roll_samples_left_ = 0;
  int32_t roll_hits_left_ = 0;
  float dens_norm_ = 0.f;
  float open_norm_ = 0.f;
  float tone_norm_ = 0.55f;
  float tune_norm_ = 0.5f;
  float decay_norm_ = 0.5f;
  float mix_ = 1.f;
  float bpm_ = 120.f;
  float internal_tick_phase_ = 0.f;
  float clock_ratio_ = 1.f;
  float dc_prev_in_ = 0.f;
  float dc_prev_out_ = 0.f;
  bool running_ = false;
  bool use_host_clock_ = false;
};
