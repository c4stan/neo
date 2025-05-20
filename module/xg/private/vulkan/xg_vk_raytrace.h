#pragma once

#include <xg.h>

#include "xg_vk.h"

#include <std_allocator.h>

typedef struct {
#if xg_vk_enable_nv_raytracing_ext_m
    VkAccelerationStructureNV vk_handle;
    xg_alloc_t alloc;
#else
    VkAccelerationStructureKHR vk_handle;
    xg_buffer_h buffer;
#endif
    xg_raytrace_geometry_params_t params;
    VkGeometryNV* geometries;
} xg_vk_raytrace_geometry_t;

typedef struct {
#if xg_vk_enable_nv_raytracing_ext_m
    VkAccelerationStructureNV vk_handle;
    xg_alloc_t alloc;
#else
    VkAccelerationStructureKHR vk_handle;
    xg_buffer_h buffer;
#endif
    xg_raytrace_geometry_instance_t* instances;
    xg_raytrace_world_params_t params;
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

xg_raytrace_geometry_h xg_vk_raytrace_geometry_create ( xg_workload_h workload, uint64_t key, const xg_raytrace_geometry_params_t* params );
xg_raytrace_world_h xg_vk_raytrace_world_create ( xg_workload_h workload, uint64_t key, const xg_raytrace_world_params_t* params );
void xg_vk_raytrace_geometry_destroy ( xg_raytrace_geometry_h geometry );
void xg_vk_raytrace_world_destroy ( xg_raytrace_world_h world );

const xg_vk_raytrace_geometry_t* xg_vk_raytrace_geometry_get ( xg_raytrace_geometry_h geo );
const xg_vk_raytrace_world_t* xg_vk_raytrace_world_get ( xg_raytrace_world_h world );
