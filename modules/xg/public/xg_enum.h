#pragma once

#include <xg.h>

size_t xg_format_size ( xg_format_e format );

const char* xg_format_str ( xg_format_e format );
const char* xg_color_space_str ( xg_color_space_e space );
const char* xg_present_mode_str ( xg_present_mode_e mode );

xg_pipeline_stage_f xg_shading_stage_to_pipeline_stage ( xg_shading_stage_e stage );
bool xg_memory_access_is_write ( xg_memory_access_f access );

bool xg_format_has_depth ( xg_format_e format );
bool xg_format_has_stencil ( xg_format_e format );
