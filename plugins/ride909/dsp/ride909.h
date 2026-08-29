#pragma once

/*
 * File: ride909.h
 *
 * Tempo-synced TR-909 ride layer for NTS-3.
 * Hold the pad to gate a techno ride wash on off-beats 3-7-11-15.
 * Quarter-note steps 1-5-9-13 sidechain-pump the tail.
 * X is 909 Tune: analog clock rate through the Ride ROM, zero-order hold,
 * no interpolation. Decay shortens as pitch rises, matching the hardware.
 * Y is kick sidechain amount. Depth is dry/wet.
 *
 * Voice path follows the 9090 Ride section of the TR-909 voicing board:
 *   variable clock -> 4040/4520 address -> 6-bit ROM -> resistor DAC
 *   -> address-derived anti-log VCA -> analog reconstruction LPFs
 */

#include "processor.h"
#include "macros.h"
#include "ride909_pcm.h"
#include "runtime.h"
#include <stdint.h>

static const float kRide909EnvLut[64] = {
  1.00000000f, 0.95652874f, 0.91494723f, 0.87517332f,
  0.83712843f, 0.80073740f, 0.76592834f, 0.73263247f,
  0.70078401f, 0.67032005f, 0.64118039f, 0.61330747f,
  0.58664622f, 0.56114397f, 0.53675033f, 0.51341712f,
  0.49109823f, 0.46974957f, 0.44932896f, 0.42979607f,
  0.41111229f, 0.39324072f, 0.37614605f, 0.35979451f,
  0.34415379f, 0.32919299f, 0.31488255f, 0.30119421f,
  0.28810092f, 0.27557681f, 0.26359714f, 0.25213824f,
  0.24117747f, 0.23069318f, 0.22066466f, 0.21107209f,
  0.20189652f, 0.19311982f, 0.18472466f, 0.17669445f,
  0.16901332f, 0.16166609f, 0.15463826f, 0.14791594f,
  0.14148585f, 0.13533528f, 0.12945209f, 0.12382464f,
  0.11844183f, 0.11329301f, 0.10836802f, 0.10365713f,
  0.09915102f, 0.09484080f, 0.09071795f, 0.08677433f,
  0.08300214f, 0.07939393f, 0.07594258f, 0.07264126f,
  0.06948345f, 0.06646292f, 0.06357369f, 0.06081006f
};

class Ride909 : public Processor
{
public:
  static constexpr uint32_t kVoiceCount = 4U;
  static constexpr uint32_t kStepsPerBar = 16U;
  static constexpr float kPitchRangeSemitones = 12.f;
  static constexpr float kMaxPumpDepth = 0.92f;
  static constexpr float kPumpHoldFraction = 0.22f;
  static constexpr float kPumpReleaseSixteenths = 2.25f;
  static constexpr float kDacMid = 32.f;
  static constexpr float kDacScale = 1.f / 32.f;
  static constexpr float kVoiceGain = 0.42f;
  // Host rate is fixed at 48 kHz. Coeffs are 1-exp(-2π fc/fs) from 9090 RC poles.
  static constexpr float kRomPhaseInc = 30000.f / 48000.f;
  static constexpr float kLpfACoeff = 0.5378f;  // 5.9 kHz
  static constexpr float kLpfBCoeff = 0.9549f;  // 23.7 kHz
  static constexpr float kHpfCoeff = 0.99608f;  // 30 Hz DC block

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    PITCH = 0U,
    PUMP,
    MIX,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case PITCH:
      pitch_norm_ = (static_cast<float>(value) - 512.f) * (1.f / 512.f);
      updateClockRatio();
      break;
    case PUMP:
      pump_amount_ = param_10bit_to_f32(value);
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
    (void)index;
    (void)value;
    return nullptr;
  }

  void init(float *) override final
  {
    pitch_norm_ = 0.f;
    pump_amount_ = 0.f;
    pump_floor_gain_ = 1.f;
    pump_hold_samples_ = 0U;
    pump_gain_ = 1.f;
    mix_ = 1.f;
    bpm_ = 120.f;
    running_ = false;
    use_host_clock_ = false;
    tick_counter_ = 0U;
    internal_tick_phase_ = 0.f;
    dc_prev_in_ = 0.f;
    dc_prev_out_ = 0.f;
    updateClockRatio();
    resetVoices();
  }

  void reset() override final
  {
    running_ = false;
    pump_gain_ = 1.f;
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

  void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    (void)id;
    (void)x;
    (void)y;

    if (phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
        phase == k_unit_touch_phase_stationary)
    {
      if (!running_)
      {
        running_ = true;
        triggerRide();
      }
      return;
    }

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      running_ = false;
      pump_gain_ = 1.f;
      resetVoices();
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    if (!use_host_clock_)
      advanceInternalClock(frames);

    const float dry_gain = 1.f - mix_;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      advancePumpEnvelope();
      const float shaped_pump = pump_gain_ * (1.f - pump_amount_ * 0.35f + pump_amount_ * 0.35f * pump_gain_);
      const float wet = renderVoices() * mix_ * shaped_pump;
      const float mixed_left = in[0] * dry_gain + wet;
      const float mixed_right = in[1] * dry_gain + wet;
      out[0] = mixed_left;
      out[1] = mixed_right;
      in += 2;
      out += 2;
    }
  }

private:
  struct Voice
  {
    bool active = false;
    float rom_phase = 0.f;
    float phase_inc = 0.f;
    float lpf_a = 0.f;
    float lpf_b = 0.f;
  };

  static uint32_t stepOneBased(uint32_t counter)
  {
    return ((counter - 1U) % kStepsPerBar) + 1U;
  }

  static bool isKickStep(uint32_t counter)
  {
    switch (stepOneBased(counter))
    {
    case 1U:
    case 5U:
    case 9U:
    case 13U:
      return true;
    default:
      return false;
    }
  }

  static bool isRideStep(uint32_t counter)
  {
    switch (stepOneBased(counter))
    {
    case 3U:
    case 7U:
    case 11U:
    case 15U:
      return true;
    default:
      return false;
    }
  }

  static uint8_t readPcm6(uint32_t sample_index)
  {
    const uint32_t bit_index = sample_index * 6U;
    const uint32_t byte_index = bit_index >> 3;
    const uint32_t shift = bit_index & 7U;
    const uint32_t pair = static_cast<uint32_t>(kRide909PcmPacked[byte_index]) |
                          (static_cast<uint32_t>(kRide909PcmPacked[byte_index + 1U]) << 8);
    return static_cast<uint8_t>((pair >> shift) & 0x3FU);
  }

  void triggerPump()
  {
    if (pump_amount_ <= 0.f)
      return;

    pump_floor_gain_ = 1.f - pump_amount_ * kMaxPumpDepth;
    pump_gain_ = pump_floor_gain_;

    if (bpm_ <= 0.f)
      return;

    const float sample_rate = getSampleRate();
    const float samples_per_16th = sample_rate * 60.f / (bpm_ * 4.f);
    pump_hold_samples_ = static_cast<uint32_t>(samples_per_16th * kPumpHoldFraction);
  }

  void advancePumpEnvelope()
  {
    if (pump_gain_ >= 1.f && pump_hold_samples_ == 0U)
    {
      pump_gain_ = 1.f;
      return;
    }

    if (bpm_ <= 0.f)
      return;

    const float sample_rate = getSampleRate();
    const float samples_per_16th = sample_rate * 60.f / (bpm_ * 4.f);
    if (samples_per_16th <= 0.f)
      return;

    if (pump_hold_samples_ > 0U)
    {
      --pump_hold_samples_;
      pump_gain_ = pump_floor_gain_;
      return;
    }

    const float release_samples = samples_per_16th * kPumpReleaseSixteenths;
    float step = 3.f / release_samples;
    if (step > 1.f)
      step = 1.f;
    pump_gain_ += (1.f - pump_gain_) * step;
    if (pump_gain_ > 1.f)
      pump_gain_ = 1.f;
  }

  static float exp2Approx(float x)
  {
    const float u = x * 0.69314718f;
    return 1.f + u * (1.f + u * (0.5f + u * (0.16666667f + u * 0.041666668f)));
  }

  void updateClockRatio()
  {
    clock_ratio_ = exp2Approx(pitch_norm_ * (kPitchRangeSemitones / 12.f));
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

    if (isKickStep(counter))
      triggerPump();

    if (isRideStep(counter))
      triggerRide();
  }

  void advanceInternalClock(uint32_t frames)
  {
    if (!running_ || bpm_ <= 0.f)
      return;

    const float sample_rate = getSampleRate();
    const float samples_per_tick = sample_rate * 60.f / (bpm_ * 4.f);
    internal_tick_phase_ += static_cast<float>(frames);

    while (internal_tick_phase_ >= samples_per_tick)
    {
      internal_tick_phase_ -= samples_per_tick;
      ++tick_counter_;
      handleTick(tick_counter_);
    }
  }

  void triggerRide()
  {
    Voice &voice = voices_[next_voice_index_];
    next_voice_index_ = (next_voice_index_ + 1U) % kVoiceCount;
    voice.active = true;
    voice.rom_phase = 0.f;
    voice.phase_inc = kRomPhaseInc * clock_ratio_;
    voice.lpf_a = 0.f;
    voice.lpf_b = 0.f;
  }

  float renderVoice(Voice &voice)
  {
    const uint32_t sample_index = static_cast<uint32_t>(voice.rom_phase);
    if (sample_index >= kRide909PcmLength)
    {
      voice.active = false;
      return 0.f;
    }

    const uint8_t pcm_code = readPcm6(sample_index);
    const float dac = (static_cast<float>(pcm_code) - kDacMid) * kDacScale;
    const float env = kRide909EnvLut[sample_index >> 9];
    const float vca = dac * env * kVoiceGain;

    voice.lpf_a += kLpfACoeff * (vca - voice.lpf_a);
    voice.lpf_b += kLpfBCoeff * (voice.lpf_a - voice.lpf_b);

    voice.rom_phase += voice.phase_inc;
    if (voice.rom_phase >= static_cast<float>(kRide909PcmLength))
      voice.active = false;

    return voice.lpf_b;
  }

  float renderVoices()
  {
    float sum = 0.f;

    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
    {
      Voice &voice = voices_[voiceIndex];
      if (!voice.active)
        continue;

      sum += renderVoice(voice);
    }

    const float blocked = sum - dc_prev_in_ + kHpfCoeff * dc_prev_out_;
    dc_prev_in_ = sum;
    dc_prev_out_ = blocked;
    return blocked;
  }

  Voice voices_[kVoiceCount];
  uint32_t next_voice_index_ = 0U;
  uint32_t tick_counter_ = 0U;
  float pitch_norm_ = 0.f;
  float clock_ratio_ = 1.f;
  float dc_prev_in_ = 0.f;
  float dc_prev_out_ = 0.f;
  float pump_amount_ = 0.f;
  float pump_floor_gain_ = 1.f;
  float pump_gain_ = 1.f;
  uint32_t pump_hold_samples_ = 0U;
  float mix_ = 1.f;
  float bpm_ = 120.f;
  float internal_tick_phase_ = 0.f;
  bool running_ = false;
  bool use_host_clock_ = false;
};
