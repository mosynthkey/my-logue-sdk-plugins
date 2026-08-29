#pragma once

/*
 * File: loopkey.h
 *
 * Keyboard-controlled micro-looper oscillator for NTS-1 mkII.
 * Records external audio input and loops it; MIDI note selects loop length.
 * Tempo sync, gate recording, and gradual evolution via 4PPQN clock.
 *
 */

#include "processor.h"
#include "macros.h"
#include <math.h>
#include <stdint.h>

class LoopKey : public Processor
{
public:
  static constexpr uint32_t kMaxBufferSamples = 8192U;
  static constexpr uint32_t kCrossfadeSamples = 48U;
  static constexpr float kReferenceNote = 60.f;
  static constexpr float kTwoPi = 6.283185307179586f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    MODE = 0U,
    DIV,
    MIX,
    FEED,
    EVOL,
    NUM_PARAMS
  };

  enum
  {
    MODE_SYNC = 0,
    MODE_GATE,
    MODE_FREE,
    NUM_MODES
  };

  enum class State : uint8_t
  {
    Idle,
    Armed,
    Recording,
    Playing
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case MODE:
    {
      uint32_t mode = static_cast<uint32_t>(value);
      if (mode >= NUM_MODES)
        mode = NUM_MODES - 1;
      mode_ = static_cast<uint8_t>(mode);
      break;
    }
    case DIV:
    {
      int32_t clamped = value;
      if (clamped < 0)
        clamped = 0;
      if (clamped > 7)
        clamped = 7;
      div_index_ = static_cast<uint8_t>(clamped);
      break;
    }
    case MIX:
      mix_ = param_10bit_to_f32(value);
      break;
    case FEED:
      feed_ = param_10bit_to_f32(value);
      break;
    case EVOL:
      evol_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    static const char *mode_names[NUM_MODES] = {"SYNC", "GATE", "FREE"};
    static const char *div_names[8] = {"1/32", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1bar"};

    if (index == MODE && value >= 0 && value < NUM_MODES)
      return mode_names[value];
    if (index == DIV && value >= 0 && value < 8)
      return div_names[value];
    return nullptr;
  }

  void init(float *) override final
  {
    mode_ = MODE_SYNC;
    div_index_ = 3;
    mix_ = 1.f;
    feed_ = 0.f;
    evol_ = 0.3f;
    bpm_ = 120.f;
    playback_rate_ = 1.f;
    pitch_rate_ = 1.f;
    bend_rate_ = 1.f;
    pressure_boost_ = 0.f;
    use_host_clock_ = false;
    tick_counter_ = 0U;
    evolution_phase_ = 0.f;
    reverse_once_ = false;
    clearBuffer();
    resetTransport();
  }

  void reset() override final
  {
    state_ = State::Idle;
    active_note_ = 0xFF;
    write_pos_ = 0U;
    read_pos_ = 0.f;
    record_target_ = 0U;
    crossfade_remain_ = 0U;
    armed_pending_ = false;
    gate_open_ = false;
  }

  void setPitch(float w0)
  {
    const float ref_w0 = kTwoPi * 261.625565f / getSampleRate();
    pitch_rate_ = w0 / ref_w0;
    if (pitch_rate_ < 0.25f)
      pitch_rate_ = 0.25f;
    if (pitch_rate_ > 4.f)
      pitch_rate_ = 4.f;
    updatePlaybackRate();
  }

  void noteOn(uint8_t note, uint8_t velo) override final
  {
    active_note_ = note;
    input_gain_ = (static_cast<float>(velo) + 1.f) * (1.f / 128.f);

    if (state_ == State::Playing)
    {
      const uint32_t new_length = computeLoopLength(note);
      if (new_length > 0U && new_length <= kMaxBufferSamples)
      {
        loop_length_ = new_length;
        if (loop_length_ > recorded_length_)
          loop_length_ = recorded_length_;
        if (read_pos_ >= static_cast<float>(loop_length_))
          read_pos_ = 0.f;
      }
      return;
    }

    record_target_ = computeLoopLength(note);
    if (record_target_ == 0U)
      record_target_ = 1024U;
    if (record_target_ > kMaxBufferSamples)
      record_target_ = kMaxBufferSamples;

    write_pos_ = 0U;
    read_pos_ = 0.f;
    crossfade_remain_ = 0U;

    if (mode_ == MODE_GATE)
    {
      gate_open_ = true;
      state_ = State::Recording;
      return;
    }

    if (mode_ == MODE_SYNC && use_host_clock_)
    {
      armed_pending_ = true;
      state_ = State::Armed;
      return;
    }

    state_ = State::Recording;
  }

  void noteOff(uint8_t note) override final
  {
    if (note != active_note_ && note != 0xFF)
      return;

    if (mode_ == MODE_GATE && gate_open_)
    {
      gate_open_ = false;
      finalizeRecording(write_pos_);
      return;
    }

    if (state_ == State::Recording && mode_ != MODE_GATE)
    {
      finalizeRecording(record_target_);
    }
  }

  void allNoteOff() override final
  {
    if (mode_ == MODE_GATE && gate_open_)
    {
      gate_open_ = false;
      finalizeRecording(write_pos_);
      return;
    }
    reset();
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 20.f && tempo < 999.f)
      bpm_ = tempo;
  }

  void tempo4ppqnTick(uint32_t counter) override final
  {
    use_host_clock_ = true;
    tick_counter_ = counter;

    if (armed_pending_ && state_ == State::Armed)
    {
      armed_pending_ = false;
      state_ = State::Recording;
      write_pos_ = 0U;
    }

    if (state_ == State::Playing && evol_ > 0.f)
      applyEvolution(counter);
  }

  void pitchBend(uint16_t bend)
  {
    const float normalized = (static_cast<float>(bend) - 8192.f) * (1.f / 8192.f);
    bend_rate_ = powf(2.f, normalized * (1.f / 12.f));
    updatePlaybackRate();
  }

  void aftertouch(uint8_t note, uint8_t press) override
  {
    (void)note;
    pressure_boost_ = static_cast<float>(press) * (1.f / 127.f);
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    if (!use_host_clock_)
      advanceInternalClock(frames);

    const float dry_gain = 1.f - mix_;
    const float wet_gain = mix_;
    float effective_feed = feed_ + pressure_boost_ * evol_ * 0.5f;
    if (effective_feed > 1.f)
      effective_feed = 1.f;
    effective_feed_clamp_ = effective_feed;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float input_sample = 0.f;
      if (in)
      {
        input_sample = (in[0] + in[1]) * 0.5f * input_gain_;
        in += 2;
      }

      if (state_ == State::Recording)
        recordSample(input_sample);

      float wet = 0.f;
      if (state_ == State::Playing && loop_length_ > 0U)
        wet = playSample(input_sample, effective_feed_clamp_);

      out[0] = input_sample * dry_gain + wet * wet_gain;
      ++out;
    }
  }

private:
  static float divisionFactor(uint8_t div_index)
  {
    static const float kDivisors[8] = {
        1.f / 32.f,
        1.f / 16.f,
        1.f / 12.f,
        1.f / 8.f,
        1.f / 6.f,
        1.f / 4.f,
        1.f / 2.f,
        1.f,
    };
    if (div_index >= 8U)
      div_index = 7U;
    return kDivisors[div_index];
  }

  uint32_t computeLoopLength(uint8_t note) const
  {
    const float note_offset = static_cast<float>(note) - kReferenceNote;

    if (mode_ == MODE_FREE)
    {
      const float ms = 50.f * powf(2.f, note_offset * (1.f / 6.f));
      uint32_t samples = static_cast<uint32_t>(ms * (getSampleRate() * 0.001f) + 0.5f);
      if (samples < 256U)
        samples = 256U;
      if (samples > kMaxBufferSamples)
        samples = kMaxBufferSamples;
      return samples;
    }

    int32_t div = static_cast<int32_t>(div_index_) - static_cast<int32_t>(note_offset * 0.5f);
    if (div < 0)
      div = 0;
    if (div > 7)
      div = 7;

    const float quarter_seconds = 60.f / bpm_;
    const float length_seconds = quarter_seconds * divisionFactor(static_cast<uint8_t>(div));
    uint32_t samples = static_cast<uint32_t>(length_seconds * getSampleRate() + 0.5f);
    if (samples < 256U)
      samples = 256U;
    if (samples > kMaxBufferSamples)
      samples = kMaxBufferSamples;
    return samples;
  }

  void clearBuffer()
  {
    for (uint32_t sampleIndex = 0; sampleIndex < kMaxBufferSamples; ++sampleIndex)
      buffer_[sampleIndex] = 0;
  }

  void resetTransport()
  {
    state_ = State::Idle;
    active_note_ = 0xFF;
    loop_length_ = 0U;
    recorded_length_ = 0U;
    write_pos_ = 0U;
    read_pos_ = 0.f;
    record_target_ = 0U;
    crossfade_remain_ = 0U;
    armed_pending_ = false;
    gate_open_ = false;
    evolution_rate_ = 1.f;
    reverse_once_ = false;
  }

  void finalizeRecording(uint32_t captured_length)
  {
    if (captured_length < 256U)
    {
      state_ = State::Idle;
      return;
    }

    loop_length_ = captured_length;
    recorded_length_ = captured_length;
    if (loop_length_ > kMaxBufferSamples)
      loop_length_ = kMaxBufferSamples;

    read_pos_ = 0.f;
    crossfade_remain_ = 0U;
    state_ = State::Playing;
  }

  void recordSample(float sample)
  {
    if (write_pos_ >= kMaxBufferSamples)
    {
      finalizeRecording(kMaxBufferSamples);
      return;
    }

    buffer_[write_pos_] = floatToInt16(sample);
    ++write_pos_;

    if (mode_ != MODE_GATE && write_pos_ >= record_target_)
      finalizeRecording(write_pos_);
  }

  float playSample(float input_sample, float feedback)
  {
    const uint32_t index_a = static_cast<uint32_t>(read_pos_);
    const uint32_t index_b = (index_a + 1U) % loop_length_;
    const float frac = read_pos_ - static_cast<float>(index_a);

    float sample = int16ToFloat(buffer_[index_a]) * (1.f - frac) + int16ToFloat(buffer_[index_b]) * frac;

    if (crossfade_remain_ > 0U)
    {
      const float fade = static_cast<float>(crossfade_remain_) / static_cast<float>(kCrossfadeSamples);
      const float head = int16ToFloat(buffer_[0]);
      sample = sample * (1.f - fade) + head * fade;
      --crossfade_remain_;
    }
    else if (index_a + kCrossfadeSamples >= loop_length_)
    {
      crossfade_remain_ = kCrossfadeSamples;
    }

    if (feedback > 0.f && input_sample != 0.f)
    {
      const float overdub = sample + input_sample * feedback;
      buffer_[index_a] = floatToInt16(overdub);
    }

    const float direction = reverse_once_ ? -1.f : 1.f;
    read_pos_ += playback_rate_ * evolution_rate_ * direction;

    if (read_pos_ >= static_cast<float>(loop_length_))
    {
      read_pos_ -= static_cast<float>(loop_length_);
      if (reverse_once_)
        reverse_once_ = false;
    }
    else if (read_pos_ < 0.f)
    {
      read_pos_ += static_cast<float>(loop_length_);
    }

    return sample;
  }

  void applyEvolution(uint32_t counter)
  {
    evolution_phase_ += evol_ * 0.04f;
    if (evolution_phase_ > kTwoPi)
      evolution_phase_ -= kTwoPi;

    evolution_rate_ = 1.f + evol_ * 0.03f * sinf(evolution_phase_);

    if ((counter & 15U) == 0U && evol_ > 0.5f)
      reverse_once_ = true;

    if ((counter & 3U) == 0U)
    {
      const float feed_mod = 1.f + evol_ * 0.15f * sinf(evolution_phase_ * 2.f);
      effective_feed_clamp_ = feed_ * feed_mod;
      if (effective_feed_clamp_ > 1.f)
        effective_feed_clamp_ = 1.f;
    }
  }

  void advanceInternalClock(uint32_t frames)
  {
    internal_samples_ += frames;
    const float samples_per_tick = getSampleRate() * 60.f / (bpm_ * 4.f);
    while (internal_samples_ >= samples_per_tick)
    {
      internal_samples_ -= samples_per_tick;
      tempo4ppqnTick(tick_counter_);
      ++tick_counter_;
    }
  }

  void updatePlaybackRate()
  {
    playback_rate_ = pitch_rate_ * bend_rate_;
  }

  static int16_t floatToInt16(float sample)
  {
    if (sample > 1.f)
      sample = 1.f;
    if (sample < -1.f)
      sample = -1.f;
    return static_cast<int16_t>(sample * 32767.f);
  }

  static float int16ToFloat(int16_t sample)
  {
    return static_cast<float>(sample) * (1.f / 32768.f);
  }

  int16_t buffer_[kMaxBufferSamples];

  State state_ = State::Idle;
  uint8_t mode_ = MODE_SYNC;
  uint8_t div_index_ = 3;
  uint8_t active_note_ = 0xFF;
  bool gate_open_ = false;
  bool armed_pending_ = false;
  bool use_host_clock_ = false;
  bool reverse_once_ = false;

  uint32_t loop_length_ = 0U;
  uint32_t recorded_length_ = 0U;
  uint32_t write_pos_ = 0U;
  uint32_t record_target_ = 0U;
  uint32_t tick_counter_ = 0U;
  uint32_t crossfade_remain_ = 0U;

  float read_pos_ = 0.f;
  float bpm_ = 120.f;
  float mix_ = 1.f;
  float feed_ = 0.f;
  float evol_ = 0.3f;
  float input_gain_ = 1.f;
  float playback_rate_ = 1.f;
  float pitch_rate_ = 1.f;
  float bend_rate_ = 1.f;
  float pressure_boost_ = 0.f;
  float evolution_phase_ = 0.f;
  float evolution_rate_ = 1.f;
  float effective_feed_clamp_ = 0.f;
  float internal_samples_ = 0.f;
};
