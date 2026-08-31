/*
 * File: unit.cc
 *
 * NTS-1 mkII oscillator unit interface for AirHorn
 */

#include "airhorn.h"
#include "unit_osc.h"
#include "utils/int_math.h"

static AirHorn s_airhorn_instance;

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

  if (desc->samplerate != s_airhorn_instance.getSampleRate())
    return k_unit_err_samplerate;

  if (desc->input_channels != 2 || desc->output_channels != 1)
    return k_unit_err_geometry;

  context = static_cast<const unit_runtime_osc_context_t *>(desc->hooks.runtime_context);

  s_airhorn_instance.init(nullptr);
  s_airhorn_instance.setStereoMix(false);

  for (uint8_t paramIndex = 0; paramIndex < UNIT_OSC_MAX_PARAM_COUNT; ++paramIndex)
    cached_values[paramIndex] = static_cast<int32_t>(unit_header.params[paramIndex].init);

  for (uint8_t paramIndex = 0; paramIndex < unit_header.num_params; ++paramIndex)
    s_airhorn_instance.setParameter(paramIndex, cached_values[paramIndex]);

  return k_unit_err_none;
}

__unit_callback void unit_teardown()
{
  s_airhorn_instance.teardown();
}

__unit_callback void unit_reset()
{
  s_airhorn_instance.reset();
}

__unit_callback void unit_resume()
{
  s_airhorn_instance.resume();
}

__unit_callback void unit_suspend()
{
  s_airhorn_instance.suspend();
}

__unit_callback void unit_render(const float *in, float *out, uint32_t frames)
{
  (void)context;
  s_airhorn_instance.process(in, out, frames);
}

__unit_callback void unit_set_param_value(uint8_t id, int32_t value)
{
  value = clipminmaxi32(unit_header.params[id].min, value, unit_header.params[id].max);
  cached_values[id] = value;
  s_airhorn_instance.setParameter(id, value);
}

__unit_callback int32_t unit_get_param_value(uint8_t id)
{
  return cached_values[id];
}

__unit_callback const char *unit_get_param_str_value(uint8_t id, int32_t value)
{
  value = clipminmaxi32(unit_header.params[id].min, value, unit_header.params[id].max);
  return s_airhorn_instance.getParameterStrValue(id, value);
}

__unit_callback void unit_note_on(uint8_t note, uint8_t velo)
{
  s_airhorn_instance.noteOn(note, velo);
}

__unit_callback void unit_note_off(uint8_t note)
{
  s_airhorn_instance.noteOff(note);
}

__unit_callback void unit_all_note_off()
{
  s_airhorn_instance.allNoteOff();
}

__unit_callback void unit_set_tempo(uint32_t tempo)
{
  (void)tempo;
}

__unit_callback void unit_tempo_4ppqn_tick(uint32_t counter)
{
  (void)counter;
}

__unit_callback void unit_pitch_bend(uint16_t bend)
{
  (void)bend;
}

__unit_callback void unit_channel_pressure(uint8_t press)
{
  (void)press;
}

__unit_callback void unit_aftertouch(uint8_t note, uint8_t press)
{
  (void)note;
  (void)press;
}
