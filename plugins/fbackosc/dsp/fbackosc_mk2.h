#pragma once

/*
 * File: fbackosc_mk2.h
 *
 * microKORG2 multi-voice Feedback oscillator adapter.
 *
 */

#include "fbackosc_engine.h"
#include "macros.h"
#include "runtime.h"
#include "unit_osc.h"
#include "utils/io_ops.h"
#include <stdint.h>

class FBackOscMk2
{
public:
  enum
  {
    kHarmonics = 0U,
    kFeedback,
    kNumParams
  };

  int8_t Init(const unit_runtime_desc_t *desc)
  {
    if (!desc)
      return k_unit_err_undef;

    if (desc->target != unit_header.target)
      return k_unit_err_target;

    if (!UNIT_API_IS_COMPAT(desc->api))
      return k_unit_err_api_version;

    if (desc->samplerate != 48000)
      return k_unit_err_samplerate;

    runtime_desc_ = *desc;

    FBackOscEngine::Params params;
    params.harmonics = 0.5f;
    params.feedback = 0.45f;

    for (uint32_t voiceIndex = 0; voiceIndex < kMk2MaxVoices; ++voiceIndex)
    {
      engines_[voiceIndex].setParams(params);
      engines_[voiceIndex].reset();
      engines_[voiceIndex].randomizePhase();
    }

    for (uint8_t paramIndex = 0; paramIndex < kNumParams; ++paramIndex)
      cached_values_[paramIndex] = static_cast<int32_t>(unit_header.params[paramIndex].init);

    return k_unit_err_none;
  }

  void Teardown() {}

  void Reset()
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kMk2MaxVoices; ++voiceIndex)
    {
      engines_[voiceIndex].reset();
      engines_[voiceIndex].randomizePhase();
    }
  }

  void Resume() { Reset(); }

  void Suspend() {}

  void Process(float *out, uint32_t frames)
  {
    const unit_runtime_osc_context_t *context =
        static_cast<const unit_runtime_osc_context_t *>(runtime_desc_.hooks.runtime_context);

    for (uint32_t voiceIndex = 0; voiceIndex < context->voiceLimit; ++voiceIndex)
    {
      const uint8_t noteWhole = static_cast<uint8_t>(context->pitch[voiceIndex]);
      const float noteFrac = context->pitch[voiceIndex] - static_cast<float>(noteWhole);
      const float w0 = osc_w0f_for_note(noteWhole, static_cast<uint8_t>(noteFrac * 255.f));
      const float note = context->pitch[voiceIndex];
      engines_[voiceIndex].setPitch(w0, note);
      ProcessVoice(out, voiceIndex, frames, context);
    }
  }

  void setParameter(uint8_t index, int32_t value)
  {
    if (index >= kNumParams)
      return;

    cached_values_[index] = value;

    FBackOscEngine::Params params = engines_[0].getParams();
    switch (index)
    {
    case kHarmonics:
      params.harmonics = param_10bit_to_f32(value);
      break;
    case kFeedback:
      params.feedback = param_10bit_to_f32(value);
      break;
    default:
      return;
    }

    for (uint32_t voiceIndex = 0; voiceIndex < kMk2MaxVoices; ++voiceIndex)
      engines_[voiceIndex].setParams(params);
  }

  int32_t getParameterValue(uint8_t index) const
  {
    if (index >= kNumParams)
      return 0;
    return cached_values_[index];
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const
  {
    (void)index;
    (void)value;
    return nullptr;
  }

private:
  void ProcessVoice(float *out, uint32_t voiceIndex, uint32_t frames,
                    const unit_runtime_osc_context_t *context)
  {
    const int offset = GetBufferOffset(context, voiceIndex, frames);
    FBackOscEngine &engine = engines_[voiceIndex];

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      const float mono = engine.render();
      write_oscillator_output_x1(out, mono, offset, context->outputStride, sampleIndex, voiceIndex);
    }
  }

  unit_runtime_desc_t runtime_desc_;
  FBackOscEngine engines_[kMk2MaxVoices];
  int32_t cached_values_[kNumParams] = {};
};
