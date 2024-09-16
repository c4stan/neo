#pragma once

#include <xg.h>

#include "xg_vk.h"

#include <std_allocator.h>

typedef struct {
    xg_buffer_h buffer;
    VkAccelerationStructureKHR vk_handle;
    xg_raytrace_geometry_params_t params;
} xg_vk_raytrace_geometry_t;

typedef struct {
    xg_buffer_h buffer;
    VkAccelerationStructureKHR vk_handle;
    xg_raytrace_world_params_t params;
    xg_buffer_h shader_instance_buffer;
} xg_vk_raytrace_world_t; // TODO rename to something else?

#define xg_vk_raytrace_max_cmd_buffers_m 32

typedef struct {
    xg_vk_raytrace_geometry_t* geometries_array;
    xg_vk_raytrace_geometry_t* geometries_freelist;
    uint64_t* geometries_bitset;

    xg_vk_raytrace_world_t* worlds_array;
    xg_vk_raytrace_world_t* worlds_freelist;
    uint64_t* worlds_bitset;
} xg_vk_raytrace_state_t;

void xg_vk_raytrace_load ( xg_vk_raytrace_state_t* state );
void xg_vk_raytrace_reload ( xg_vk_raytrace_state_t* state );
void xg_vk_raytrace_unload ( void );

xg_raytrace_geometry_h xg_vk_raytrace_geometry_create ( const xg_raytrace_geometry_params_t* params );
xg_raytrace_world_h xg_vk_raytrace_world_create ( const xg_raytrace_world_params_t* params );

const xg_vk_raytrace_world_t* xg_vk_raytrace_world_get ( xg_raytrace_world_h world );
