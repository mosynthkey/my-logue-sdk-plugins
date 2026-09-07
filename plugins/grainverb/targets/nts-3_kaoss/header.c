/*
 * File: header.c
 *
 * NTS-3 generic effect unit header for GrainVerb
 */

#include "unit_genericfx.h"
#include "dev_id.h"

const __unit_header genericfx_unit_header_t unit_header = {
    .common = {
        .header_size = sizeof(genericfx_unit_header_t),
        .target = UNIT_TARGET_PLATFORM | k_unit_module_genericfx,
        .api = UNIT_API_VERSION,
        .dev_id = MLSA_DEV_ID,
        .unit_id = 0x00000035U,
        .version = MLSA_VERSION_EXPERIMENTAL,
        .name = "GrainVerb",
        .num_params = 8,
        .params = {
            {0, 1023, 0, 665, k_unit_param_type_none, 0, 0, 0, {"FEEL"}},
            {0, 1023, 0, 768, k_unit_param_type_none, 0, 0, 0, {"SIZE"}},
            {0, 1000, 0, 1000, k_unit_param_type_percent, 1, 1, 0, {"MIX"}},
            {0, 1023, 0, 614, k_unit_param_type_none, 0, 0, 0, {"ENV"}},
            {0, 4, 0, 1, k_unit_param_type_strings, 0, 0, 0, {"SYNC"}},
            {0, 1023, 0, 410, k_unit_param_type_none, 0, 0, 0, {"SPRD"}},
            {0, 1023, 0, 460, k_unit_param_type_none, 0, 0, 0, {"TONE"}},
            {0, 1023, 0, 100, k_unit_param_type_none, 0, 0, 0, {"REVS"}}},
    },
    .default_mappings = {
        {k_genericfx_param_assign_x, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 665},
        {k_genericfx_param_assign_y, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 768},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1000, 1000},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 614},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 4, 1},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 410},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 460},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 100},
    },
};
