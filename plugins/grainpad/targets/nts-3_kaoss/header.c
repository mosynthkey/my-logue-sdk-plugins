/*
 * File: header.c
 *
 * NTS-3 generic effect unit header for GrainPad
 */

#include "unit_genericfx.h"
#include "dev_id.h"

const __unit_header genericfx_unit_header_t unit_header = {
    .common = {
        .header_size = sizeof(genericfx_unit_header_t),
        .target = UNIT_TARGET_PLATFORM | k_unit_module_genericfx,
        .api = UNIT_API_VERSION,
        .dev_id = MLSA_DEV_ID,
        .unit_id = 0x0000000CU,
        .version = MLSA_VERSION_EXPERIMENTAL,
        .name = "GrainPad",
        .num_params = 7,
        .params = {
            {0, 1023, 0, 512, k_unit_param_type_none, 0, 0, 0, {"SCAN"}},
            {0, 1023, 0, 512, k_unit_param_type_none, 0, 0, 0, {"PITCH"}},
            {0, 1000, 0, 1000, k_unit_param_type_percent, 1, 1, 0, {"MIX"}},
            {0, 1023, 0, 410, k_unit_param_type_none, 0, 0, 0, {"SIZE"}},
            {0, 1023, 0, 512, k_unit_param_type_none, 0, 0, 0, {"DENS"}},
            {0, 1023, 0, 358, k_unit_param_type_none, 0, 0, 0, {"SPRAY"}},
            {0, 1023, 0, 512, k_unit_param_type_none, 0, 0, 0, {"DECAY"}}},
    },
    .default_mappings = {
        {k_genericfx_param_assign_x, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 512},
        {k_genericfx_param_assign_y, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 512},
        {k_genericfx_param_assign_depth, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1000, 1000},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 410},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 512},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 358},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 512},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 0, 0},
    },
};
