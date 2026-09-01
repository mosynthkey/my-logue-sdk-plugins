/*
 * File: unit.cc
 *
 * NTS-3 generic effect unit interface for GrainPad
 */

#include "grainpad.h"
#include "unit_genericfx.h"
#include "utils/int_math.h"

static GrainPad s_grainpad_instance;

static int32_t cached_values[UNIT_GENERICFX_MAX_PARAM_COUNT];

__unit_callback int8_t unit_init(const unit_runtime_desc_t *desc)
{
  if (!desc)
    return k_unit_err_undef;

  if (desc->target != unit_header.common.target)
    return k_unit_err_target;

  if (!UNIT_API_IS_COMPAT(desc->api))
    return k_unit_err_api_version;

  if (desc->samplerate != s_grainpad_instance.getSampleRate())
    return k_unit_err_samplerate;

  if (desc->input_channels != 2 || desc->output_channels != 2)
    return k_unit_err_geometry;

  s_grainpad_instance.init(nullptr);
  s_grainpad_instance.setStereoMix(true);

  for (uint8_t paramIndex = 0; paramIndex < UNIT_GENERICFX_MAX_PARAM_COUNT; ++paramIndex)
    cached_values[paramIndex] = static_cast<int32_t>(unit_header.common.params[paramIndex].init);

  for (uint8_t paramIndex = 0; paramIndex < unit_header.common.num_params; ++paramIndex)
    s_grainpad_instance.setParameter(paramIndex, cached_values[paramIndex]);

  return k_unit_err_none;
}

__unit_callback void unit_teardown()
{
  s_grainpad_instance.teardown();
}

__unit_callback void unit_reset()
{
  s_grainpad_instance.reset();
}

__unit_callback void unit_resume()
{
  s_grainpad_instance.resume();
}

__unit_callback void unit_suspend()
{
  s_grainpad_instance.suspend();
}

__unit_callback void unit_render(const float *in, float *out, uint32_t frames)
{
  s_grainpad_instance.process(in, out, frames);
}

__unit_callback void unit_set_param_value(uint8_t id, int32_t value)
{
  value = clipminmaxi32(unit_header.common.params[id].min, value, unit_header.common.params[id].max);
  cached_values[id] = value;
  s_grainpad_instance.setParameter(id, value);
}

__unit_callback int32_t unit_get_param_value(uint8_t id)
{
  return cached_values[id];
}

__unit_callback const char *unit_get_param_str_value(uint8_t id, int32_t value)
{
  value = clipminmaxi32(unit_header.common.params[id].min, value, unit_header.common.params[id].max);
  return s_grainpad_instance.getParameterStrValue(id, value);
}

__unit_callback void unit_touch_event(uint8_t id, uint8_t phase, uint32_t x, uint32_t y)
{
  s_grainpad_instance.touchEvent(id, phase, x, y);
}

__unit_callback void unit_set_tempo(uint32_t tempo)
{
  (void)tempo;
}

__unit_callback void unit_tempo_4ppqn_tick(uint32_t counter)
{
  (void)counter;
}
