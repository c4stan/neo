#pragma once

#include <xg.h>

#include "xg_vk.h"

#include <std_allocator.h>
#include <std_mutex.h>

typedef struct {
    xg_texture_view_t desc;
    xg_format_e format;
    uint32_t mip_count;
    uint32_t array_count;
    VkImageAspectFlags aspect;
    VkImage image;
    char debug_name[xg_debug_name_size_m];
} xg_vk_texture_view_params_t;

typedef struct {
    VkImageView vk_handle;
    xg_vk_texture_view_params_t params;
} xg_vk_texture_view_t;

// TODO rename enum to something more meaningful/specific?
typedef enum {
    xg_vk_texture_state_reserved_m,
    xg_vk_texture_state_created_m
} xg_vk_texture_state_e;

typedef struct {
    VkImage                 vk_handle;
    xg_alloc_t              allocation;
    xg_texture_params_t     params;
    xg_texture_flag_bit_e   flags;

    xg_vk_texture_view_t    default_view;

    union {
        struct {
            xg_vk_texture_view_t* array;
        } mips;
        // TODO use std_map_t? and store <xg_vk_texture_view_t, xg_texture_view_t> in payload
        struct {
            // TODO not implemented yet
            std_hash_map_t* map; // xg_texture_view_t -> xg_vk_texture_view_t
        } table;
    } external_views;

    xg_vk_texture_state_e   state;
    xg_texture_aspect_e     default_aspect;
} xg_vk_texture_t;

typedef struct {
    xg_vk_texture_t* textures_array;
    xg_vk_texture_t* textures_freelist;
    uint64_t* textures_bitset;
    std_mutex_t textures_mutex;
    xg_texture_h default_textures[xg_vk_max_devices_m][xg_default_texture_count_m];
} xg_vk_texture_state_t;

void xg_vk_texture_load ( xg_vk_texture_state_t* state );
void xg_vk_texture_reload ( xg_vk_texture_state_t* state );
void xg_vk_texture_unload ( void );

void xg_vk_texture_activate_device ( xg_device_h device, xg_workload_h workload );

// create = reserve + alloc
xg_texture_h xg_texture_create ( const xg_texture_params_t* params );
xg_texture_h xg_texture_reserve ( const xg_texture_params_t* params );
bool xg_texture_alloc ( xg_texture_h texture );

bool xg_texture_get_info ( xg_texture_info_t* info, xg_texture_h texture );

bool xg_texture_destroy ( xg_texture_h texture );

bool xg_texture_resize ( xg_texture_h texture, size_t width, size_t height );

xg_texture_h xg_vk_texture_register_swapchain_texture ( const xg_texture_params_t* params, VkImage image );
void xg_vk_texture_update_swapchain_texture ( xg_texture_h texture, const xg_texture_params_t* params, VkImage image );
void xg_vk_texture_unregister_swapchain_texture ( xg_texture_h texture );

const xg_vk_texture_t* xg_vk_texture_get ( xg_texture_h handle );
const xg_vk_texture_view_t* xg_vk_texture_get_view ( xg_texture_h texture, xg_texture_view_t view );

xg_memory_requirement_t xg_texture_memory_requirement ( const xg_texture_params_t* params );

/*
    TODO
        check VkMemoryDedicatedRequirements when allocating resources, create a dedicated memory allocation if needed instead of the shared allocator
        see https://asawicki.info/articles/memory_management_vulkan_direct3d_12.php5 "Dedicated allocations"
*/

xg_texture_h xg_texture_get_default ( xg_device_h device, xg_default_texture_e texture_enum );
