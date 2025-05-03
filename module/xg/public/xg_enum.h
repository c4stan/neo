#pragma once

#include <xg.h>

size_t xg_format_size ( xg_format_e format );

const char* xg_format_str ( xg_format_e format );
const char* xg_color_space_str ( xg_color_space_e space );
const char* xg_present_mode_str ( xg_present_mode_e mode );
const char* xg_texture_layout_str ( xg_texture_layout_e layout ); 
const char* xg_cmd_queue_str ( xg_cmd_queue_e queue );
const char* xg_pipeline_stage_str ( xg_pipeline_stage_bit_e stage );
const char* xg_memory_access_str ( xg_memory_access_bit_e access );
const char* xg_memory_type_str ( xg_memory_type_e type );

xg_pipeline_stage_bit_e xg_shading_stage_to_pipeline_stage ( xg_shading_stage_e stage );
bool xg_memory_access_is_write ( xg_memory_access_bit_e access );

bool xg_format_has_depth ( xg_format_e format );
bool xg_format_has_stencil ( xg_format_e format );
