#pragma once

#include <xg.h>

#include "xg_vk.h"

#include <std_mutex.h>

typedef enum {
    xg_vk_buffer_state_reserved_m,
    xg_vk_buffer_state_created_m
} xg_vk_buffer_state_e;

typedef struct {
    VkBuffer                vk_handle;
    xg_alloc_t              allocation;
    uint64_t                offset;
    xg_buffer_params_t      params;
    xg_vk_buffer_state_e    state;
} xg_vk_buffer_t;

typedef struct {
    xg_vk_buffer_t* buffers_array;
    xg_vk_buffer_t* buffers_freelist;
    std_mutex_t buffers_mutex;
} xg_vk_buffer_state_t;

void xg_vk_buffer_load ( xg_vk_buffer_state_t* state );
void xg_vk_buffer_reload ( xg_vk_buffer_state_t* state );
void xg_vk_buffer_unload ( void );

xg_buffer_h xg_buffer_create ( const xg_buffer_params_t* params );

xg_buffer_h xg_buffer_reserve ( const xg_buffer_params_t* params );
bool xg_buffer_alloc ( xg_buffer_h buffer );

bool xg_buffer_get_info ( xg_buffer_info_t* info, xg_buffer_h buffer );
bool xg_buffer_destroy ( xg_buffer_h buffer );

const xg_vk_buffer_t* xg_vk_buffer_get ( xg_buffer_h handle );
