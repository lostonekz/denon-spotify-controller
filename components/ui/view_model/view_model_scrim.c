#include "view_model_scrim.h"

uint8_t view_model_scrim_opacity_for_luma(uint8_t avg_luma)
{
    if (avg_luma <= VIEW_MODEL_SCRIM_LUMA_MIDPOINT) {
        return (uint8_t)VIEW_MODEL_SCRIM_BASELINE_OPA;
    }

    uint32_t span     = 255u - VIEW_MODEL_SCRIM_LUMA_MIDPOINT;
    uint32_t over      = (uint32_t)avg_luma - VIEW_MODEL_SCRIM_LUMA_MIDPOINT;
    uint32_t opa_span  = VIEW_MODEL_SCRIM_CEILING_OPA - VIEW_MODEL_SCRIM_BASELINE_OPA;
    uint32_t opa        = VIEW_MODEL_SCRIM_BASELINE_OPA + (over * opa_span) / span;

    return (uint8_t)opa;
}
