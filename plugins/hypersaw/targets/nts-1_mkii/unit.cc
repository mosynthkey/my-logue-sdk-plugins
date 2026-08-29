/*
    BSD 3-Clause License

    Copyright (c) 2023, KORG INC.
    Copyright (c) 2026, HyperSaw contributors
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this
      list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

    * Neither the name of the copyright holder nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
    OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

//*/

/*
 * File: unit.cc
 *
 * NTS-1 mkII oscillator unit interface for HyperSaw
 *
 */

#include "hypersaw.h"
#include "unit_osc.h"
#include "utils/int_math.h"

static HyperSaw s_hypersaw_instance;
static int32_t cached_values[UNIT_OSC_MAX_PARAM_COUNT];
static const unit_runtime_osc_context_t *context;

__unit_callback int8_t unit_init(const unit_runtime_desc_t *desc)
{
  if (!desc)
    return k_unit_err_undef;

  if (desc->target != unit_header.target)
    return k_unit_err_target;

  if (!UNIT_API_IS_COMPAT(desc->api))
    return k_unit_err_api_version;

  if (desc->samplerate != s_hypersaw_instance.getSampleRate())
    return k_unit_err_samplerate;

  if (desc->input_channels != 2 || desc->output_channels != 1)
    return k_unit_err_geometry;

  context = static_cast<const unit_runtime_osc_context_t *>(desc->hooks.runtime_context);

  s_hypersaw_instance.init(nullptr);

  for (uint8_t paramIndex = 0; paramIndex < UNIT_OSC_MAX_PARAM_COUNT; ++paramIndex)
    cached_values[paramIndex] = static_cast<int32_t>(unit_header.params[paramIndex].init);

  for (uint8_t paramIndex = 0; paramIndex < unit_header.num_params; ++paramIndex)
    s_hypersaw_instance.setParameter(paramIndex, cached_values[paramIndex]);

  return k_unit_err_none;
}

__unit_callback void unit_teardown()
{
  s_hypersaw_instance.teardown();
}

__unit_callback void unit_reset()
{
  s_hypersaw_instance.reset();
}

__unit_callback void unit_resume()
{
  s_hypersaw_instance.resume();
}

__unit_callback void unit_suspend()
{
  s_hypersaw_instance.suspend();
}

__unit_callback void unit_render(const float *in, float *out, uint32_t frames)
{
  const uint8_t note = static_cast<uint8_t>(context->pitch >> 8);
  const uint8_t mod = static_cast<uint8_t>(context->pitch & 0xFF);
  s_hypersaw_instance.setPitch(osc_w0f_for_note(note, mod));
  s_hypersaw_instance.setNote(static_cast<float>(note) + mod * (1.f / 256.f));
  s_hypersaw_instance.process(in, out, frames);
}

__unit_callback void unit_set_param_value(uint8_t id, int32_t value)
{
  value = clipminmaxi32(unit_header.params[id].min, value, unit_header.params[id].max);
  cached_values[id] = value;
  s_hypersaw_instance.setParameter(id, value);
}

__unit_callback int32_t unit_get_param_value(uint8_t id)
{
  return cached_values[id];
}

__unit_callback const char *unit_get_param_str_value(uint8_t id, int32_t value)
{
  value = clipminmaxi32(unit_header.params[id].min, value, unit_header.params[id].max);
  return s_hypersaw_instance.getParameterStrValue(id, value);
}

__unit_callback void unit_note_on(uint8_t note, uint8_t velo)
{
  s_hypersaw_instance.noteOn(note, velo);
}

__unit_callback void unit_note_off(uint8_t note)
{
  s_hypersaw_instance.noteOff(note);
}

__unit_callback void unit_all_note_off()
{
  s_hypersaw_instance.allNoteOff();
}

__unit_callback void unit_set_tempo(uint32_t tempo)
{
  float bpm = (tempo >> 16) + (tempo & 0xFFFF) / static_cast<float>(0x10000);
  s_hypersaw_instance.setTempo(bpm);
}

__unit_callback void unit_tempo_4ppqn_tick(uint32_t counter)
{
  s_hypersaw_instance.tempo4ppqnTick(counter);
}

__unit_callback void unit_pitch_bend(uint16_t bend)
{
  s_hypersaw_instance.pitchBend(bend);
}

__unit_callback void unit_channel_pressure(uint8_t press)
{
  s_hypersaw_instance.channelPressure(press);
}

__unit_callback void unit_aftertouch(uint8_t note, uint8_t press)
{
  s_hypersaw_instance.aftertouch(note, press);
}
