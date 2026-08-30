#pragma once

/*
 * File: tapeosc_engine.h
 *
 * Tape-style varispeed oscillator. A rolling waveform buffer is written at
 * target pitch while the read head follows playback_rate, producing tape
 * motor start/stop pitch sweeps instead of a simple pitch envelope.
 *
 */

#include "osc_api.h"
#include "utils/float_math.h"
#include <math.h>
#include <stdint.h>

class TapeOscEngine
{
public:
  static constexpr uint32_t kBufferSize = 4096U;
  static constexpr float kTwoPi = 6.283185307179586f;
  static constexpr float kOutputTrim = 0.62f;
  static constexpr float kMinStartSec = 0.01f;
  static constexpr float kMaxStartSec = 2.f;
  static constexpr float kMinStopSec = 0.05f;
  static constexpr float kMaxStopSec = 4.f;
  static constexpr float kMinLpfHz = 180.f;
  static constexpr float kMaxLpfHz = 14000.f;
  static constexpr float kMinWearLpfHz = 2200.f;
  static constexpr float kWowHz = 0.55f;
  static constexpr float kFlutterHz = 6.5f;

  enum Waveform : uint8_t
  {
    WAVEFORM_SAW = 0U,
    WAVEFORM_SQUARE,
    WAVEFORM_SINE,
    WAVEFORM_TRIANGLE,
    NUM_WAVEFORMS
  };

  enum class TransportState : uint8_t
  {
    Idle = 0U,
    Starting,
    Running,
    Stopping
  };

  struct Params
  {
    Waveform waveform = WAVEFORM_SAW;
    float start_norm = 0.42f;
    float stop_norm = 0.55f;
    float grit = 0.35f;
    float wear = 0.f;
    float wow = 0.f;
  };

  void reset()
  {
    clearBuffer();
    source_phase_ = 0.f;
    write_pos_ = 0.f;
    read_pos_ = 0.f;
    playback_rate_ = 0.f;
    lpf_state_ = 0.f;
    wear_lpf_state_ = 0.f;
    wow_phase_ = 0.f;
    flutter_phase_ = 0.f;
    wow_mix_ = 0.f;
    hold_counter_ = 0.f;
    held_sample_ = 0.f;
    transport_state_ = TransportState::Idle;
    active_ = false;
  }

  void randomizePhase()
  {
    source_phase_ = osc_white();
  }

  void setParams(const Params &params)
  {
    params_ = params;
    updateTransportCoeffs();
    updateWearCoeff();
  }

  const Params &getParams() const { return params_; }

  void setPitch(float w0, float note)
  {
    base_w0_ = w0;
    bl_idx_ = osc_bl_saw_idx(note);
    par_idx_ = osc_bl_par_idx(note);
  }

  void beginStart()
  {
    clearBuffer();
    write_pos_ = 0.f;
    read_pos_ = 0.f;
    playback_rate_ = 0.f;
    lpf_state_ = 0.f;
    wear_lpf_state_ = 0.f;
    hold_counter_ = 0.f;
    held_sample_ = 0.f;
    transport_state_ = TransportState::Starting;
    active_ = true;
  }

  void beginStop()
  {
    if (!active_ || transport_state_ == TransportState::Idle ||
        transport_state_ == TransportState::Stopping)
      return;

    transport_state_ = TransportState::Stopping;
  }

  float render()
  {
    if (!active_ && transport_state_ == TransportState::Idle)
      return 0.f;

    const float source_sample = renderSource(source_phase_);
    source_phase_ += base_w0_;
    source_phase_ -= floorf(source_phase_);

    const uint32_t write_index = static_cast<uint32_t>(write_pos_) % kBufferSize;
    buffer_[write_index] = source_sample;
    write_pos_ += 1.f;
    if (write_pos_ >= static_cast<float>(kBufferSize))
      write_pos_ -= static_cast<float>(kBufferSize);

    advanceTransport();
    advanceWowFlutter();

    const float raw_sample = readBuffer(read_pos_);

    const float wow_scale = params_.wow * playback_rate_;
    const float effective_rate = playback_rate_ * (1.f + wow_scale * wow_mix_);
    read_pos_ += effective_rate;
    while (read_pos_ >= static_cast<float>(kBufferSize))
      read_pos_ -= static_cast<float>(kBufferSize);

    updateLpfCoeff();
    lpf_state_ += lpf_coeff_ * (raw_sample - lpf_state_);

    const float motor_gain = 0.15f + 0.85f * sqrtf(fmaxf(playback_rate_, 0.f));
    float output = lpf_state_ * motor_gain;

    output = applyWear(output);

    return output * kOutputTrim;
  }

  bool isActive() const { return active_; }

private:
  static float mapTimeSec(float norm, float min_sec, float max_sec)
  {
    const float clamped = (norm < 0.f) ? 0.f : ((norm > 1.f) ? 1.f : norm);
    return min_sec * powf(max_sec / min_sec, clamped);
  }

  void clearBuffer()
  {
    for (uint32_t sampleIndex = 0; sampleIndex < kBufferSize; ++sampleIndex)
      buffer_[sampleIndex] = 0.f;
  }

  void updateTransportCoeffs()
  {
    const float start_sec = mapTimeSec(params_.start_norm, kMinStartSec, kMaxStartSec);
    const float stop_sec = mapTimeSec(params_.stop_norm, kMinStopSec, kMaxStopSec);
    start_coeff_ = 1.f - expf(-1.f / (start_sec * getSampleRate()));
    stop_coeff_ = 1.f - expf(-1.f / (stop_sec * getSampleRate()));
  }

  static float getSampleRate() { return static_cast<float>(k_samplerate); }

  float renderSource(float phase) const
  {
    switch (params_.waveform)
    {
    case WAVEFORM_SQUARE:
      return osc_bl2_sqrf(phase, bl_idx_);
    case WAVEFORM_SINE:
      return osc_sinf(phase);
    case WAVEFORM_TRIANGLE:
      return osc_bl2_parf(phase, par_idx_);
    case WAVEFORM_SAW:
    default:
      return osc_bl2_sawf(phase, bl_idx_);
    }
  }

  float readBuffer(float position) const
  {
    float wrapped = position;
    while (wrapped >= static_cast<float>(kBufferSize))
      wrapped -= static_cast<float>(kBufferSize);
    while (wrapped < 0.f)
      wrapped += static_cast<float>(kBufferSize);

    const uint32_t index_a = static_cast<uint32_t>(wrapped) % kBufferSize;
    const uint32_t index_b = (index_a + 1U) % kBufferSize;
    const float frac = wrapped - floorf(wrapped);
    const float linear_sample = linintf(frac, buffer_[index_a], buffer_[index_b]);
    const float zoh_sample = buffer_[index_a];
    return linintf(params_.grit, linear_sample, zoh_sample);
  }

  void advanceTransport()
  {
    switch (transport_state_)
    {
    case TransportState::Starting:
      playback_rate_ += (1.f - playback_rate_) * start_coeff_;
      if (playback_rate_ > 0.9995f)
      {
        playback_rate_ = 1.f;
        transport_state_ = TransportState::Running;
      }
      break;

    case TransportState::Running:
      playback_rate_ = 1.f;
      break;

    case TransportState::Stopping:
      playback_rate_ += (0.f - playback_rate_) * stop_coeff_;
      if (playback_rate_ < 0.00005f)
      {
        playback_rate_ = 0.f;
        transport_state_ = TransportState::Idle;
        active_ = false;
      }
      break;

    case TransportState::Idle:
    default:
      playback_rate_ = 0.f;
      break;
    }
  }

  void updateLpfCoeff()
  {
    const float cutoff_hz = kMinLpfHz + playback_rate_ * (kMaxLpfHz - kMinLpfHz);
    lpf_coeff_ = 1.f - expf((-kTwoPi * cutoff_hz) / getSampleRate());
  }

  void updateWearCoeff()
  {
    const float cutoff_hz = kMaxLpfHz - params_.wear * (kMaxLpfHz - kMinWearLpfHz);
    wear_lpf_coeff_ = 1.f - expf((-kTwoPi * cutoff_hz) / getSampleRate());
  }

  void advanceWowFlutter()
  {
    const float sample_rate = getSampleRate();
    wow_phase_ += kWowHz / sample_rate;
    flutter_phase_ += kFlutterHz / sample_rate;
    wow_phase_ -= floorf(wow_phase_);
    flutter_phase_ -= floorf(flutter_phase_);

    const float wow_lfo = osc_sinf(wow_phase_);
    const float flutter_lfo = osc_sinf(flutter_phase_);
    wow_mix_ = wow_lfo * 0.72f + flutter_lfo * 0.28f;
  }

  static float saturateWear(float sample, float wear)
  {
    const float drive = 1.f + wear * 3.2f;
    const float driven = sample * drive;
    const float abs_sample = fabsf(driven);
    if (abs_sample < 1.f)
      return driven;
    return driven / (1.f + abs_sample - 1.f);
  }

  float applyWear(float sample)
  {
    const float wear = params_.wear;
    if (wear <= 0.f)
      return sample;

    wear_lpf_state_ += wear_lpf_coeff_ * (sample - wear_lpf_state_);
    float output = linintf(wear, sample, wear_lpf_state_);

    output = saturateWear(output, wear);

    const float hold_stride = 1.f + wear * wear * 64.f;
    hold_counter_ += 1.f;
    if (hold_counter_ >= hold_stride)
    {
      hold_counter_ -= hold_stride;
      held_sample_ = output;
    }
    output = linintf(wear * 0.55f, output, held_sample_);

    const float hiss = osc_white() * wear * 0.09f;
    output += hiss;

    if (wear > 0.35f && osc_white() > (1.f - wear * 0.015f))
      output *= 0.2f;

    return output;
  }

  Params params_;
  float buffer_[kBufferSize] = {};
  float source_phase_ = 0.f;
  float write_pos_ = 0.f;
  float read_pos_ = 0.f;
  float playback_rate_ = 0.f;
  float start_coeff_ = 0.f;
  float stop_coeff_ = 0.f;
  float base_w0_ = 0.f;
  float bl_idx_ = 0.f;
  float par_idx_ = 0.f;
  float lpf_state_ = 0.f;
  float lpf_coeff_ = 0.f;
  float wear_lpf_state_ = 0.f;
  float wear_lpf_coeff_ = 0.f;
  float wow_phase_ = 0.f;
  float flutter_phase_ = 0.f;
  float wow_mix_ = 0.f;
  float hold_counter_ = 0.f;
  float held_sample_ = 0.f;
  TransportState transport_state_ = TransportState::Idle;
  bool active_ = false;
};
