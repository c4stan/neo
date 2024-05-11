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
} xg_vk_raytrace_world_t;

typedef struct {
    std_memory_h geometries_memory_handle;
    xg_vk_raytrace_geometry_t* geometries_array;
    xg_vk_raytrace_geometry_t* geometries_freelist;

    std_memory_h worlds_memory_handle;
    xg_vk_raytrace_world_t* worlds_array;
    xg_vk_raytrace_world_t* worlds_freelist;
} xg_vk_raytrace_state_t;

xg_raytrace_geometry_h xg_vk_raytrace_create_geometry ( const xg_raytrace_geometry_params_t* params );
xg_raytrace_world_h xg_vk_raytrace_create_world ( const xg_raytrace_world_params_t* params );
