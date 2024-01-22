#pragma once

#include <xg.h>

#include <std_buffer.h>
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
    //xg_device_h device;
    xg_workload_h workload;
    std_memory_h memory_handle;
    std_queue_local_t cmd_headers_allocator;    // xg_cmd_header_t
    std_queue_local_t cmd_args_allocator;
} xg_cmd_buffer_t;

typedef enum {
    // Graphics pipeline
    xg_cmd_graphics_streams_bind_m,
    xg_cmd_graphics_pipeline_state_bind_m,
    xg_cmd_graphics_render_textures_bind_m,

    xg_cmd_graphics_streams_submit_m,
    xg_cmd_graphics_streams_submit_indexed_m,
    xg_cmd_graphics_streams_submit_instanced_m,

    xg_cmd_graphics_pipeline_state_set_viewport_m,
    xg_cmd_graphics_pipeline_state_set_scissor_m,

    // Copy
    xg_cmd_copy_buffer_m,
    xg_cmd_copy_texture_m,
    xg_cmd_copy_buffer_to_texture_m,
    xg_cmd_copy_texture_to_buffer_m,
    /*
        TODO:
            COPY
            COMPUTE
    */
    // Compute pipeline
    xg_cmd_compute_dispatch_m,
    xg_cmd_compute_pipeline_state_bind_m,
    xg_cmd_compute_pipeline_resource_bind_m,
    // Misc?
    xg_cmd_pipeline_resource_bind_m,
    xg_cmd_pipeline_resource_group_bind_m,
    xg_cmd_pipeline_constant_write_m,
    xg_cmd_barrier_set_m,
    xg_cmd_texture_clear_m,
    xg_cmd_texture_depth_stencil_clear_m,
    xg_cmd_start_debug_capture_m,
    xg_cmd_stop_debug_capture_m,
    xg_cmd_begin_debug_region_m,
    xg_cmd_end_debug_region_m,
    xg_cmd_write_timestamp_m,
} xg_cmd_type_e;

#define xg_cmd_buffer_cmd_alignment_m 8

int xg_vk_cmd_buffer_header_compare_f ( const void* a, const void* b );

typedef struct {
    // Abusing the fact that x64 pointers have their top 16 bits unused
    // https://stackoverflow.com/questions/31522582/can-i-use-some-bits-of-pointer-x86-64-for-custom-data-and-how-if-possible
    // Args contains a memory address.
    uint64_t args : 48;
    uint64_t type : 16;
    uint64_t key;
} xg_cmd_header_t;

typedef struct {
    uint32_t bindings_count;
    xg_vertex_stream_binding_t bindings[];
} xg_cmd_graphics_streams_bind_t;

int xg_vertex_stream_binding_cmp_f ( const void* a, const void* b );

typedef struct {
    xg_graphics_pipeline_state_h pipeline;
} xg_cmd_graphics_pipeline_state_bind_t;

typedef struct {
    xg_resource_binding_set_e set;
    uint32_t buffer_count;
    uint32_t texture_count;
    uint32_t sampler_count;
    //xg_buffer_resource_binding_t[]
    //xg_texture_resource_binding_t[]
    //xg_sampler_resource_binding_t[]
} xg_cmd_pipeline_resource_bind_t;

typedef struct {
    xg_resource_binding_set_e set;
    xg_pipeline_resource_group_h group;
} xg_cmd_pipeline_resource_group_bind_t;

typedef struct {
    xg_shading_stage_b stages;
    uint32_t offset;
    uint32_t size;
    byte_t data[];
} xg_cmd_pipeline_constant_data_write_t;

typedef struct {
    size_t                      render_targets_count;
    xg_depth_stencil_binding_t  depth_stencil;
    xg_render_target_binding_t  render_targets[];
} xg_cmd_graphics_render_texture_bind_t;

typedef struct {
    uint32_t count;
} xg_cmd_graphics_invoke_t;

typedef struct {
    uint32_t base;
    uint32_t count;
} xg_cmd_graphics_streams_submit_t;

typedef struct {
    uint32_t ibuffer_base;
    uint32_t index_count;
    xg_buffer_h ibuffer;
} xg_cmd_graphics_streams_submit_indexed_t;

typedef struct {
    uint32_t vertex_base;
    uint32_t vertex_count;
    uint32_t instance_base;
    uint32_t instance_count;
} xg_cmd_graphics_streams_submit_instanced_t;

typedef struct {
    xg_viewport_state_t viewport;
} xg_cmd_graphics_pipeline_state_set_viewport_t;

typedef struct {
    xg_scissor_state_t scissor;
} xg_cmd_graphics_pipeline_state_set_scissor_t;

typedef struct {
    uint32_t workgroup_count_x;
    uint32_t workgroup_count_y;
    uint32_t workgroup_count_z;
} xg_cmd_compute_dispatch_t;

typedef struct {
    xg_compute_pipeline_state_h pipeline;
} xg_cmd_compute_pipeline_state_bind_t;

typedef struct {
    xg_buffer_h source;
    xg_buffer_h destination;
} xg_cmd_copy_buffer_t;

typedef struct {
    xg_texture_copy_resource_t source;
    xg_texture_copy_resource_t destination;
    uint32_t mip_count;
    uint32_t array_count;
    xg_texture_aspect_e aspect;
    xg_sampler_filter_e filter;
} xg_cmd_copy_texture_t;

typedef struct {
    xg_buffer_h source;
    uint64_t source_offset;
    xg_texture_h destination;
    uint32_t mip_base;
    uint32_t array_base;
    uint32_t array_count;
    //xg_texture_aspect_e aspect;
    //xg_texture_layout_e layout;
} xg_cmd_copy_buffer_to_texture_t;

typedef struct {
    xg_texture_h source;
    uint32_t mip_base;
    uint32_t array_base;
    uint32_t array_count;
    xg_buffer_h destination;
    uint64_t destination_offset;
} xg_cmd_copy_texture_to_buffer_t;

// The current layout is sent in along with the cmd
/*
typedef struct {
    xg_texture_h texture;
    xg_texture_layout_e old_layout;
    xg_texture_layout_e new_layout;
    xg_memory_barrier_t memory;
} xg_cmd_memory_barrier_texture_t;
*/

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
    xg_pipeline_stage_f dependency;
} xg_cmd_write_timestamp_t;
#endif
//

typedef struct {
    std_memory_h        cmd_buffers_memory_handle;
    xg_cmd_buffer_t*    cmd_buffers_array;
    xg_cmd_buffer_t*    cmd_buffers_freelist;
    uint64_t            allocated_cmd_buffers_count;
    std_mutex_t         cmd_buffers_mutex;
    //std_queue_shared_t  cmd_buffers_queue;  // mpmc xg_cmd_buffer_t idx queue - indexes into the pool buffer. TODO make this a mpsc when the API is there
} xg_cmd_buffer_state_t;

void xg_cmd_buffer_load ( xg_cmd_buffer_state_t* state );
void xg_cmd_buffer_reload ( xg_cmd_buffer_state_t* state );
void xg_cmd_buffer_unload ( void );

// **
//
// Cmd buffer API
//
// **

// Returns a number of ready to record command buffers to the user. Tries to grab them from the pool, allocates new ones if necessary.
xg_cmd_buffer_h xg_cmd_buffer_open ( xg_workload_h workload );
void xg_cmd_buffer_open_n ( xg_cmd_buffer_h* cmd_buffers, size_t count, xg_workload_h workload );

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
// Each command has a key value associated that works as a global sorting key. The backend will do the sorting before GPU submission.
// Bind input vertex streams.
void xg_cmd_buffer_graphics_streams_bind ( xg_cmd_buffer_h cmd_buffer, const xg_vertex_stream_binding_t* bindings, size_t bindings_count, uint64_t key );

// Bind pipeline state.
void xg_cmd_buffer_graphics_pipeline_state_bind ( xg_cmd_buffer_h cmd_buffer, xg_graphics_pipeline_state_h pipeline, uint64_t key );

// Bind render textures.
void xg_cmd_buffer_graphics_render_textures_bind ( xg_cmd_buffer_h cmd_buffer, const xg_render_textures_binding_t* bindings, uint64_t key );

// Draw calls
void xg_cmd_buffer_graphics_streams_submit ( xg_cmd_buffer_h cmd_buffer, size_t vertex_count, size_t streams_base, uint64_t key );
void xg_cmd_buffer_graphics_streams_submit_indexed ( xg_cmd_buffer_h cmd_buffer, xg_buffer_h ibuffer, size_t index_count, size_t ibuffer_base, uint64_t key );
void xg_cmd_buffer_graphics_streams_submit_instanced ( xg_cmd_buffer_h cmd_buffer, size_t vertex_base, size_t vertex_count, size_t instance_base, size_t instance_count, uint64_t key );

void xg_cmd_buffer_graphics_pipeline_state_set_viewport ( xg_cmd_buffer_h cmd_buffer, const xg_viewport_state_t* viewport, uint64_t key );
void xg_cmd_buffer_graphics_pipeline_state_set_scissor ( xg_cmd_buffer_h cmd_buffer, const xg_scissor_state_t* scissor, uint64_t key );

// ======================================================================================= //
//                                      C O M P U T E
// ======================================================================================= //
void    xg_cmd_buffer_compute_dispatch                  ( xg_cmd_buffer_h buffer, uint32_t workgroup_count_x, uint32_t workgroup_count_y, uint32_t workgroup_count_z, uint64_t key );
void    xg_cmd_buffer_compute_pipeline_state_bind       ( xg_cmd_buffer_h buffer, xg_compute_pipeline_state_h pipeline, uint64_t key );
//void    xg_cmd_buffer_compute_pipeline_resource_bind    ( xg_cmd_buffer_h buffer, xg_pipeline_resource_bindings_t* bindings, uint64_t key );

// ======================================================================================= //
//                                         C O P Y
// ======================================================================================= //
void xg_cmd_buffer_copy_buffer ( xg_cmd_buffer_h buffer, xg_buffer_h source, xg_buffer_h dest, uint64_t key );
void xg_cmd_buffer_copy_texture ( xg_cmd_buffer_h buffer, const xg_texture_copy_params_t* params, uint64_t key );
//void xg_cmd_buffer_generate_texture_mips ( xg_cmd_buffer_h buffer, xg_texture_h texture, uint32_t mip_base, uint32_t mip_count, uint64_t key );
void xg_cmd_buffer_copy_buffer_to_texture ( xg_cmd_buffer_h buffer, const xg_buffer_to_texture_copy_params_t* params, uint64_t key );
void xg_cmd_buffer_copy_texture_to_buffer ( xg_cmd_buffer_h buffer, const xg_texture_to_buffer_copy_params_t* params, uint64_t key );

// ======================================================================================= //
//                                         M I S C
// ======================================================================================= //
void    xg_cmd_buffer_pipeline_resources_bind           ( xg_cmd_buffer_h cmd_buffer, const xg_pipeline_resource_bindings_t* bindings, uint64_t key );
void    xg_cmd_buffer_pipeline_resource_group_bind      ( xg_cmd_buffer_h cmd_buffer, xg_resource_binding_set_e set, xg_pipeline_resource_group_h group, uint64_t key );

void    xg_cmd_buffer_barrier_set                       ( xg_cmd_buffer_h cmd_buffer, const xg_barrier_set_t* barrier_set, uint64_t key );

void    xg_cmd_buffer_texture_clear                     ( xg_cmd_buffer_h cmd_buffer, xg_texture_h texture, xg_color_clear_t clear_color, uint64_t key );
void    xg_cmd_buffer_texture_depth_stencil_clear       ( xg_cmd_buffer_h cmd_buffer, xg_texture_h texture, xg_depth_stencil_clear_t clear_value, uint64_t key );

void    xg_cmd_buffer_start_debug_capture               ( xg_cmd_buffer_h cmd_buffer, xg_debug_capture_stop_time_e stop_time, uint64_t key );
void    xg_cmd_buffer_stop_debug_capture                ( xg_cmd_buffer_h cmd_buffer, uint64_t key );

void    xg_cmd_buffer_begin_debug_region                ( xg_cmd_buffer_h cmd_buffer, const char* name, uint32_t color, uint64_t key );
void    xg_cmd_buffer_end_debug_region                  ( xg_cmd_buffer_h cmd_buffer, uint64_t key );

#if 0
    void    xg_cmd_buffer_write_timestamp                   ( xg_cmd_buffer_h cmd_buffer, const xg_timestamp_query_params_t* params, uint64_t key );
#endif
