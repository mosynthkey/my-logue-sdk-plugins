/**
 *  @file header.c
 *  @brief microKORG2 FbOsc oscillator unit header
 *
 *  Copyright (c) 2026 FbOsc contributors
 *
 */

#include "unit.h"
#include "runtime.h"

#include "dev_id.h"
#include "mk2_dev_id.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_osc,
    .api = UNIT_API_VERSION,
    .dev_id = MLSA_DEV_ID,
    .unit_id = MK2_UNIT_ID_FBACKOSC,
    .version = MLSA_VERSION_EXPERIMENTAL,
    .name = "FbOsc",
    .num_presets = 0,
    .num_params = 13,
    .params = {
        {0, 1023, 0, 512, k_unit_param_type_none, 1, 0, 0, {"HARM"}},
        {0, 1023, 0, 460, k_unit_param_type_none, 1, 0, 0, {"FEED"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}}};
