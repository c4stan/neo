#pragma once

#include <xg.h>

#include <std_allocator.h>
#include <std_queue.h>
#include <std_mutex.h>

/*
    Command buffer backend pipeline
        [ merge -> sort -> chunk -> translate -> submit ]
        - Command buffers are created, filled, and returned to xg by the user.
        - Once all command buffers for a single frame/present have been returned to xg, the backend can go on and sort all of the buffers into
          a single big cmd buffer containing all of the commands for the frame.
        - The one buffer can be used to detect segments that can be used to efficiently split the buffer into separate Vulkan cmd buffers.
        - The segments are dispatched and the Vulkan cmd buffers are built in parallel.
        - Finally, all of the Vulkan cmd buffers can be submitted and followed by a present call.
        - After submission of the cmd buffers, xg keeps tracking the lifetime of those commands and waits until their execution in GPU
          is complete to finally pool back the buffers and the associated resources that got deleted.

    Notes
        - Waiting to receive all cmd buffers from the user before starting to work on them and eventually submit them to GPU introduces latency.
          This is fine, we are OK with GPU being a bit behind compared to where rendering/sim is. In fact, a frame might look something like
          [ sim N | rend N-1 | gpu N-2 ] running in parallel.
        - As we do support sorting of individual comamnds on a global level, it is probably necessary to have some sort of separation of sort keys
          from actual data, especially for the bigger commands.
        - How to implement resources management? create all resources before submitting commands for one frame, and delete them all after?
          Using a separate cmd buffer type for resource commands would introduce an annoying additional type but it also would allow to sort
          things out in the most efficient manner. Could even allocate create cmds from the top and delete cmds from the bottom.
*/

typedef struct {
    // Abusing the fact that x64 pointers have their top 16 bits unused
    // https://stackoverflow.com/questions/31522582/can-i-use-some-bits-of-pointer-x86-64-for-custom-data-and-how-if-possible
    // Args contains a memory address.
    uint64_t args : 48; // void*
    uint64_t type : 8; // xg_cmd_type_e
    uint64_t tag  : 8; // room for custom byte-sized inline arg
    uint64_t key;
} xg_cmd_header_t;

typedef struct {
    //xg_device_h device;
    xg_workload_h workload;
    // TODO why is this not a stack
    std_queue_local_t cmd_headers_allocator;    // xg_cmd_header_t
    std_queue_local_t cmd_args_allocator;
} xg_cmd_buffer_t;

typedef enum {
    xg_cmd_graphics_renderpass_begin_m,
    xg_cmd_graphics_renderpass_end_m,

    xg_cmd_draw_m,
    xg_cmd_compute_m,
    xg_cmd_raytrace_m,

    xg_cmd_copy_buffer_m,
    xg_cmd_copy_texture_m,
    xg_cmd_copy_buffer_to_texture_m,
    xg_cmd_copy_texture_to_buffer_m,

    xg_cmd_texture_clear_m,
    xg_cmd_texture_depth_stencil_clear_m,

    xg_cmd_barrier_set_m,
    xg_cmd_bind_queue_m,

    xg_cmd_start_debug_capture_m,
    xg_cmd_stop_debug_capture_m,
    xg_cmd_begin_debug_region_m,
    xg_cmd_end_debug_region_m,
    //xg_cmd_write_timestamp_m,
} xg_cmd_type_e;

#define xg_cmd_buffer_cmd_alignment_m 8

int xg_vk_cmd_buffer_header_compare_f ( const void* a, const void* b );

int xg_vertex_stream_binding_cmp_f ( const void* a, const void* b );

typedef struct {
    //xg_execution_barrier_t execution_barrier;
    uint32_t memory_barriers;
    uint32_t buffer_memory_barriers;
    uint32_t texture_memory_barriers;
    // xg_execution_barrier_t[]
    // xg_memory_barrier_t[]
    // xg_buffer_memory_barrier_t[]
    // xg_texture_memory_barrier_t[]
} xg_cmd_barrier_set_t;

typedef struct {
    xg_texture_h texture;
    xg_color_clear_t clear;
} xg_cmd_texture_clear_t;

typedef struct {
    xg_texture_h texture;
    xg_depth_stencil_clear_t clear;
} xg_cmd_texture_depth_stencil_clear_t;

typedef struct {
    xg_debug_capture_stop_time_e stop_time;
} xg_cmd_start_debug_capture_t;

typedef struct {
    const char* name; // TODO make the string allocation local?
    uint32_t color_rgba;
} xg_cmd_begin_debug_region_t;

#if 0
typedef struct {
    xg_query_buffer_h query_buffer;
    xg_pipeline_stage_bit_e dependency;
} xg_cmd_write_timestamp_t;
#endif
//

typedef struct {
    xg_cmd_buffer_t*    cmd_buffers_array;
    xg_cmd_buffer_t*    cmd_buffers_freelist;
    uint64_t*           cmd_buffers_bitset;
    uint64_t            allocated_cmd_buffers_count;
    std_mutex_t         cmd_buffers_mutex;
    //std_queue_shared_t  cmd_buffers_queue;  // mpmc xg_cmd_buffer_t idx queue - indexes into the pool buffer. TODO make this a mpsc when the API is there
} xg_cmd_buffer_state_t;

void xg_cmd_buffer_load ( xg_cmd_buffer_state_t* state );
void xg_cmd_buffer_reload ( xg_cmd_buffer_state_t* state );
void xg_cmd_buffer_unload ( void );

// **
//
// ======================================================================================= //
//                               C M D   B U F F E R   A P I
// ======================================================================================= //
//
// **
// Each command has a key value associated that works as a global sorting key. The backend will do the sorting before GPU submission.

// Returns a number of ready to record command buffers to the user. Tries to grab them from the pool, allocates new ones if necessary.
xg_cmd_buffer_h xg_cmd_buffer_open ( xg_workload_h workload );
void xg_cmd_buffer_open_n ( xg_cmd_buffer_h* cmd_buffers, size_t count, xg_workload_h workload );

// sorts all cmd headers from an array of cmd buffers into a single sorted array
// cmd_header_cap should be big enough to contain all the cmd headers contained in the passed in cmd buffers
// both cmd_headers and cmd_headers_temp need to respect cmd_header_cap
// after calling cmd_headers will contain the sorted result. cmd_headers_temp will contain garbage and can be reused or destroyed 
void xg_cmd_buffer_sort_n ( xg_cmd_header_t* cmd_headers, xg_cmd_header_t* cmd_headers_temp, size_t cmd_header_cap, const xg_cmd_buffer_t** cmd_buffers, size_t cmd_buffer_count );

#if 0
    // Queue a command buffer for later processing/submission.
    void xg_cmd_buffer_close ( xg_cmd_buffer_h* cmd_buffers, size_t count );
#endif

// Put a command buffer back in the pool and discard its content.
void xg_cmd_buffer_discard ( xg_cmd_buffer_h* cmd_buffers, size_t count );

// For interaction with the HAL submodule.
xg_cmd_buffer_t* xg_cmd_buffer_get ( xg_cmd_buffer_h cmd_buffer );

// ======================================================================================= //
//                                     G R A P H I C S
// ======================================================================================= //
// Begin/end renderpass
void xg_cmd_buffer_cmd_renderpass_begin ( xg_cmd_buffer_h cmd_buffer, uint64_t key, const xg_cmd_renderpass_params_t* params );
void xg_cmd_buffer_cmd_renderpass_end ( xg_cmd_buffer_h cmd_buffer, uint64_t key );

// Draw calls
void xg_cmd_buffer_cmd_draw ( xg_cmd_buffer_h cmd_buffer, uint64_t key, const xg_cmd_draw_params_t* params );

// ======================================================================================= //
//                                      C O M P U T E
// ======================================================================================= //
void xg_cmd_buffer_cmd_compute ( xg_cmd_buffer_h buffer, uint64_t key, const xg_cmd_compute_params_t* params );

// ======================================================================================= //
//                                     R A Y T R A C E
// ======================================================================================= //
void xg_cmd_buffer_cmd_raytrace ( xg_cmd_buffer_h buffer, uint64_t key, const xg_cmd_raytrace_params_t* params );

// ======================================================================================= //
//                                         C O P Y
// ======================================================================================= //
void xg_cmd_buffer_copy_buffer ( xg_cmd_buffer_h buffer, uint64_t key, const xg_buffer_copy_params_t* params );
void xg_cmd_buffer_copy_texture ( xg_cmd_buffer_h buffer, uint64_t key, const xg_texture_copy_params_t* params );
void xg_cmd_buffer_copy_buffer_to_texture ( xg_cmd_buffer_h buffer, uint64_t key, const xg_buffer_to_texture_copy_params_t* params );
void xg_cmd_buffer_copy_texture_to_buffer ( xg_cmd_buffer_h buffer, uint64_t key, const xg_texture_to_buffer_copy_params_t* params );

// ======================================================================================= //
//                                         M I S C
// ======================================================================================= //
void xg_cmd_buffer_barrier_set ( xg_cmd_buffer_h cmd_buffer, uint64_t key, const xg_barrier_set_t* barrier_set );
void xg_cmd_bind_queue ( xg_cmd_buffer_h cmd_buffer, uint64_t key, const xg_cmd_bind_queue_params_t* params );
void xg_cmd_buffer_texture_clear ( xg_cmd_buffer_h cmd_buffer, uint64_t key, xg_texture_h texture, xg_color_clear_t clear_color );
void xg_cmd_buffer_texture_depth_stencil_clear ( xg_cmd_buffer_h cmd_buffer, uint64_t key, xg_texture_h texture, xg_depth_stencil_clear_t clear_value );
void xg_cmd_buffer_start_debug_capture ( xg_cmd_buffer_h cmd_buffer, uint64_t key, xg_debug_capture_stop_time_e stop_time );
void xg_cmd_buffer_stop_debug_capture ( xg_cmd_buffer_h cmd_buffer, uint64_t key );
void xg_cmd_buffer_begin_debug_region ( xg_cmd_buffer_h cmd_buffer, uint64_t key, const char* name, uint32_t color );
void xg_cmd_buffer_end_debug_region ( xg_cmd_buffer_h cmd_buffer, uint64_t key );

#if 0
    void    xg_cmd_buffer_write_timestamp                   ( xg_cmd_buffer_h cmd_buffer, const xg_timestamp_query_params_t* params, uint64_t key );
#endif
