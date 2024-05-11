#pragma once

#include <xg.h>

#include "xg_vk.h"

#include <std_allocator.h>
#include <std_mutex.h>

typedef struct {
    VkSampler vk_handle;

    xg_sampler_params_t params;
} xg_vk_sampler_t;

typedef struct {
    xg_vk_sampler_t* samplers_array;
    xg_vk_sampler_t* samplers_freelist;
    std_mutex_t samplers_mutex;
    xg_sampler_h default_samplers[xg_vk_max_devices_m][xg_default_sampler_count_m];
} xg_vk_sampler_state_t;

void xg_vk_sampler_load ( xg_vk_sampler_state_t* state );
void xg_vk_sampler_reload ( xg_vk_sampler_state_t* state );
void xg_vk_sampler_unload ( void );

xg_sampler_h xg_sampler_create ( const xg_sampler_params_t* params );
xg_sampler_h xg_sampler_get_default ( xg_device_h device, xg_default_sampler_e sampler );

bool xg_sampler_get_info ( xg_sampler_h sampler, xg_sampler_info_t* info );
bool xg_sampler_destroy ( xg_sampler_h sampler );

const xg_vk_sampler_t* xg_vk_sampler_get ( xg_sampler_h sampler );
