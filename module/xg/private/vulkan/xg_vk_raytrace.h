#pragma once

#include <xg.h>

#include "xg_vk.h"

#include <std_allocator.h>

typedef struct {
    xg_device_h device;
    xg_buffer_h buffer;
    VkAccelerationStructureKHR vk_handle;
    xg_raytrace_geometry_params_t params;
} xg_vk_raytrace_geometry_t;

typedef struct {
    xg_device_h device;
    xg_buffer_h buffer;
    VkAccelerationStructureKHR vk_handle;
} xg_vk_raytrace_world_t;

typedef struct {
    xg_vk_raytrace_geometry_t* geometries_array;
    xg_vk_raytrace_geometry_t* geometries_freelist;

    xg_vk_raytrace_world_t* worlds_array;
    xg_vk_raytrace_world_t* worlds_freelist;
} xg_vk_raytrace_state_t;

xg_raytrace_geometry_h xg_vk_raytrace_geometry_create ( const xg_raytrace_geometry_params_t* params );
xg_raytrace_world_h xg_vk_raytrace_world_create ( const xg_raytrace_world_params_t* params );
