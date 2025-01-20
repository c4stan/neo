#pragma once

#include <xg.h>

#include <std_allocator.h>
#include <std_queue.h>
#include <std_mutex.h>

// TODO allocate cmds on opposide sides of the buffer depending on time_e
typedef struct {
    xg_workload_h workload;
    std_queue_local_t cmd_headers_allocator;    // xg_cmd_header_t
    std_queue_local_t cmd_args_allocator;
} xg_resource_cmd_buffer_t;

typedef enum {
    xg_resource_cmd_texture_create_m,
    xg_resource_cmd_texture_destroy_m,

    xg_resource_cmd_buffer_create_m,
    xg_resource_cmd_buffer_destroy_m,

    xg_resource_cmd_resource_bindings_update_m,
    xg_resource_cmd_resource_bindings_destroy_m,

    xg_resource_cmd_renderpass_destroy_m,

    xg_resource_cmd_queue_event_destroy_m,

    // TODO pipeline create/destroy
    //      once that is available, rewrite pipeline destrouction in xs pipeline update to use resource cmd buffers instead of tracking workloads internally?

    //xg_resource_cmd_query_buffer_create_m,
    xg_resource_cmd_timestamp_query_buffer_destroy_m,
    xg_resource_cmd_timestamp_query_buffer_clear_m,
    xg_resource_cmd_timestamp_query_buffer_readback_m,
} xg_resource_cmd_type_e;

#define xg_resource_cmd_buffer_cmd_alignment_m 8

typedef struct {
    // Abusing the fact that x64 pointers have their top 16 bits unused
    // https://stackoverflow.com/questions/31522582/can-i-use-some-bits-of-pointer-x86-64-for-custom-data-and-how-if-possible
    // Args contains a memory address.
    // TODO use a relative pointer, can preserve globality while reducing size by pre-reserving all memory for all buffers in a single virtual range
    uint64_t args : 48;
    uint64_t type : 16;
} xg_resource_cmd_header_t;

typedef struct {
    xg_texture_h texture;
} xg_resource_cmd_texture_create_t;

typedef struct {
    xg_texture_h texture;
    xg_resource_cmd_buffer_time_e destroy_time;
} xg_resource_cmd_texture_destroy_t;

typedef struct {
    xg_buffer_h buffer;
} xg_resource_cmd_buffer_create_t;

typedef struct {
    xg_buffer_h buffer;
    xg_resource_cmd_buffer_time_e destroy_time;
} xg_resource_cmd_buffer_destroy_t;

typedef struct {
    xg_resource_bindings_h group;
    uint32_t buffer_count;
    uint32_t texture_count;
    uint32_t sampler_count;
    uint32_t raytrace_world_count;
    //xg_buffer_resource_binding_t[]
    //xg_texture_resource_binding_t[]
    //xg_sampler_resource_binding_t[]
    //xg_raytrace_world_resource_binding_t[]
} xg_resource_cmd_resource_bindings_update_t;

typedef struct {
    xg_resource_bindings_h group;
    xg_resource_cmd_buffer_time_e destroy_time;
} xg_resource_cmd_resource_bindings_destroy_t;

typedef struct {
    xg_renderpass_h renderpass;
    xg_resource_cmd_buffer_time_e destroy_time;
} xg_resource_cmd_renderpass_destroy_t;

typedef struct {
    xg_queue_event_h event;
    xg_resource_cmd_buffer_time_e destroy_time;
} xg_resource_cmd_queue_event_destroy_t;

#if 0
typedef struct {
    xg_timestamp_query_buffer_h buffer;
    xg_resource_cmd_buffer_time_e time;
} xg_resource_cmd_timestamp_query_buffer_args_t;
#endif

// ---

typedef struct {
    xg_resource_cmd_buffer_t* cmd_buffers_array;
    xg_resource_cmd_buffer_t* cmd_buffers_freelist;
    uint64_t allocated_cmd_buffers_count;
    std_mutex_t cmd_buffers_mutex;
} xg_resource_cmd_buffer_state_t;

void xg_resource_cmd_buffer_load ( xg_resource_cmd_buffer_state_t* state );
void xg_resource_cmd_buffer_reload ( xg_resource_cmd_buffer_state_t* state );
void xg_resource_cmd_buffer_unload ( void );

xg_resource_cmd_buffer_h xg_resource_cmd_buffer_open ( xg_workload_h workload );
void xg_resource_cmd_buffer_open_n ( xg_resource_cmd_buffer_h* cmd_buffers, size_t count, xg_workload_h workload );

#if 0
    void xg_resource_cmd_buffer_close ( xg_resource_cmd_buffer_h* cmd_buffers, size_t count );
#endif

void xg_resource_cmd_buffer_discard ( xg_resource_cmd_buffer_h* cmd_buffers, size_t count );

xg_resource_cmd_buffer_t* xg_resource_cmd_buffer_get ( xg_resource_cmd_buffer_h cmd_buffer );

// ======================================================================================= //
//                                     R E S O U R C E
// ======================================================================================= //
xg_texture_h    xg_resource_cmd_buffer_texture_create           ( xg_resource_cmd_buffer_h cmd_buffer, const xg_texture_params_t* params );
void            xg_resource_cmd_buffer_texture_destroy          ( xg_resource_cmd_buffer_h cmd_buffer, xg_texture_h texture, xg_resource_cmd_buffer_time_e destroy_time );

xg_buffer_h     xg_resource_cmd_buffer_buffer_create            ( xg_resource_cmd_buffer_h cmd_buffer, const xg_buffer_params_t* params );
void            xg_resource_cmd_buffer_buffer_destroy           ( xg_resource_cmd_buffer_h cmd_buffer, xg_buffer_h buffer, xg_resource_cmd_buffer_time_e destroy_time );

xg_resource_bindings_h xg_resource_cmd_buffer_resource_bindings_create ( xg_resource_cmd_buffer_h cmd_buffer, const xg_resource_bindings_params_t* params );
xg_resource_bindings_h xg_resource_cmd_buffer_workload_resource_bindings_create ( xg_resource_cmd_buffer_h cmd_buffer, const xg_resource_bindings_params_t* params );
void            xg_resource_cmd_buffer_resource_bindings_update    ( xg_resource_cmd_buffer_h cmd_buffer, xg_resource_bindings_h group, const xg_pipeline_resource_bindings_t* bindings );
void            xg_resource_cmd_buffer_resource_bindings_destroy   ( xg_resource_cmd_buffer_h cmd_buffer, xg_resource_bindings_h group, xg_resource_cmd_buffer_time_e destroy_time );

void            xg_resource_cmd_buffer_graphics_renderpass_destroy ( xg_resource_cmd_buffer_h cmd_buffer, xg_renderpass_h renderpass, xg_resource_cmd_buffer_time_e destroy_time );

void            xg_resource_cmd_buffer_queue_event_destroy ( xg_resource_cmd_buffer_h cmd_buffer, xg_queue_event_h event, xg_resource_cmd_buffer_time_e destroy_time );

#if 0
    //xg_query_buffer_h xg_resource_cmd_buffer_query_buffer_create    ( xg_resource_cmd_buffer_h cmd_buffer, const xg_query_buffer_params_t* params );
    void            xg_resource_cmd_buffer_timestamp_query_buffer_clear     ( xg_resource_cmd_buffer_h cmd_buffer, xg_timestamp_query_buffer_h query_buffer, xg_resource_cmd_buffer_time_e time );
    xg_timestamp_query_buffer_results_t xg_resource_cmd_buffer_timestamp_query_buffer_readback ( xg_resource_cmd_buffer_h cmd_buffer, xg_timestamp_query_buffer_h query_buffer, xg_resource_cmd_buffer_time_e time );
    void            xg_resource_cmd_buffer_timestamp_query_buffer_destroy   ( xg_resource_cmd_buffer_h cmd_buffer, xg_timestamp_query_buffer_h query_buffer, xg_resource_cmd_buffer_time_e time );
#endif
