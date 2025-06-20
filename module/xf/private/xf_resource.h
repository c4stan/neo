#pragma once

#include <xf.h>

#include <std_array.h>
#include <std_allocator.h>

/*
    Resource flow
        user declares resources trough create_texture/buffer api, gets a xf resource handle
        user lists resource inputs and outputs when creating a node, using xf resource handles
        at graph build time, before calling the node user callback, an xg resource cmd buffer is used to create the xg resource using the parameters coming from the xf resource declaration
        the create cmd is registered on the resource cmd buffer
        the node user callback is called, the resource gets used
        once all nodes have been processed, the xg (resource) cmd buffers are submitted
        permanent resources are kept alive, next frame it won't be necessary to recreate, just grab the already existing one
*/

// Keeps track of the state of a (sub) resource
// Updated during graph execution whenever a new memory barrier is queued up for the resource
typedef struct {
    xg_texture_layout_e layout;
    xg_pipeline_stage_bit_e stage;
    xg_memory_access_bit_e access;
    xg_cmd_queue_e queue;
} xf_texture_execution_state_t;

#define xf_texture_execution_state_m( ... ) ( xf_texture_execution_state_t ) { \
    .layout = xg_texture_layout_undefined_m, \
    .stage = xg_pipeline_stage_bit_none_m, \
    .access = xg_memory_access_bit_none_m, \
    .queue = xg_cmd_queue_invalid_m, \
    ##__VA_ARGS__ \
}

typedef struct {
    xg_pipeline_stage_bit_e stage;
    xg_memory_access_bit_e access;
    xg_cmd_queue_e queue;
} xf_buffer_execution_state_t;

#define xf_buffer_execution_state_m( ... ) ( xf_buffer_execution_state_t ) { \
    .stage = xg_pipeline_stage_bit_none_m, \
    .access = xg_memory_access_bit_none_m, \
    .queue = xg_cmd_queue_invalid_m, \
    ##__VA_ARGS__ \
}

typedef struct {
    xf_texture_execution_state_t execution;
    xg_cmd_queue_e prev_queue;
    bool queue_transition;
} xf_texture_state_t; // TODO rename this to execution_state_t, the previous to transition_state_t or something

#define xf_texture_state_m( ... ) ( xf_texture_state_t ) { \
    .execution = xf_texture_execution_state_m(), \
    .prev_queue = xg_cmd_queue_invalid_m, \
    .queue_transition = false, \
    ##__VA_ARGS__ \
}

//#define xf_resource_execution_state_m( _layout, _stage, _access ) ( xf_texture_execution_state_t ) { \
//    .layout = _layout, \
//    .stage = _stage, \
//    .access = _access, \
//}

/*
    view:               mip | array | format | aspect
    execution state:    mip | array

    TODO keep 2 separate maps for external views/exec states
*/

#if 0
typedef struct {

} xf_texture_mip_views_t;

typedef struct {
    uint32_t count;
    uint32_t mask;
    uint64_t keys[32];
    uint64_t values[32]; // indexes into states
    xf_texture_subresource_state_t states[32];
} xf_texture_view_table_t;
#endif

typedef uint64_t xf_physical_texture_h;

typedef struct {
    xf_physical_texture_h physical_texture_handle;
    xg_texture_usage_bit_e required_usage;
    xf_texture_params_t params;
    uint32_t ref_count;
    bool is_multi;
} xf_texture_t;

#define xf_texture_m( ... ) ( xf_texture_t ) { \
    .physical_texture_handle = xf_null_handle_m, \
    .required_usage = xg_texture_usage_bit_none_m, \
    .params = xf_texture_params_m(), \
    .ref_count = 0, \
    .is_multi = false, \
    ##__VA_ARGS__ \
}

typedef struct {
    xg_buffer_h xg_handle;
    xf_buffer_execution_state_t state;
    xg_buffer_usage_bit_e required_usage;
    xg_buffer_usage_bit_e allowed_usage;
    xf_buffer_params_t params;
    uint32_t ref_count;
    bool is_multi;
} xf_buffer_t;

#define xf_buffer_m( ... ) ( xf_buffer_t ) { \
    .xg_handle = xg_null_handle_m, \
    .state = xf_buffer_execution_state_m(), \
    .required_usage = xg_buffer_usage_bit_none_m, \
    .allowed_usage = xg_buffer_usage_bit_none_m, \
    .params = xf_buffer_params_m(), \
    .ref_count = 0, \
    .is_multi = false, \
    ##__VA_ARGS__ \
}

typedef struct {
    xf_multi_texture_params_t params;
    uint32_t index;
    xf_texture_h textures[xf_resource_multi_texture_max_textures_m];
    xg_swapchain_h swapchain;
    uint32_t ref_count;
} xf_multi_texture_t;

#define xf_multi_texture_m( ... ) ( xf_multi_texture_t ) { \
    .params = xf_multi_texture_params_m(), \
    .index = 0, \
    .swapchain = xg_null_handle_m, \
    .ref_count = 0, \
    ##__VA_ARGS__ \
}

typedef struct {
    xf_multi_buffer_params_t params;
    uint32_t index;
    xf_buffer_h buffers[xf_resource_multi_buffer_max_buffers_m];
    xf_buffer_h alias;
    uint32_t ref_count;
} xf_multi_buffer_t;

#define xf_multi_buffer_m( ... ) ( xf_multi_buffer_t ) { \
    .params = xf_multi_buffer_params_m(), \
    .index = 0, \
    .alias = xf_null_handle_m, \
    .ref_count = 0, \
    ##__VA_ARGS__ \
}

typedef struct {
    bool is_external; // lifetime not owned by xf. never allocated, just used
    xg_texture_h handle;
    xg_texture_info_t info;
} xf_physical_texture_params_t;

#define xf_physical_texture_params_m( ... ) ( xf_physical_texture_params_t ) { \
    .is_external = false, \
    .handle = xg_null_handle_m, \
    ##__VA_ARGS__ \
}

typedef struct {
    bool is_external;
    xg_texture_h handle;
    uint32_t ref_count;
    xg_texture_info_t info;
    union {
        xf_texture_state_t shared;
        xf_texture_state_t mips[xf_resource_max_mip_levels_m]; // TODO make storage external?
        // TODO external hash table to support dynamic view access
        // OR   dyn alloc from shared big T buffer as many as you need, no waste
    } state;
} xf_physical_texture_t;

#define xf_physical_texture_m( ... ) ( xf_physical_texture_t ) { \
    .is_external = false, \
    .handle = xg_null_handle_m, \
    .info = { 0 }, \
    .ref_count = 0, \
    .state.mips = { [0 ... 15] = xf_texture_state_m() }, \
    ##__VA_ARGS__ \
}

typedef struct {
    xf_buffer_t* buffers_array;
    xf_buffer_t* buffers_freelist;
    uint64_t* buffers_bitset;

    xf_multi_buffer_t* multi_buffers_array;
    xf_multi_buffer_t* multi_buffers_freelist;
    uint64_t* multi_buffers_bitset;

    xf_texture_t* textures_array;
    xf_texture_t* textures_freelist;
    uint64_t* textures_bitset;

    xf_multi_texture_t* multi_textures_array;
    xf_multi_texture_t* multi_textures_freelist;
    uint64_t* multi_textures_bitset;

    xf_physical_texture_t* physical_textures_array;
    xf_physical_texture_t* physical_textures_freelist;
    uint64_t* physical_textures_bitset;
} xf_resource_state_t;

void xf_resource_load ( xf_resource_state_t* state );
void xf_resource_reload ( xf_resource_state_t* state );
void xf_resource_unload ( void );

// TODO rename _declare to _create?
xf_texture_h xf_resource_texture_create ( const xf_texture_params_t* params );
xf_texture_h xf_resource_texture_create_from_external ( xg_texture_h texture );
xf_buffer_h xf_resource_buffer_create ( const xf_buffer_params_t* params );

void xf_resource_texture_destroy ( xf_texture_h texture );
void xf_resource_buffer_destroy ( xf_buffer_h buffer );

void xf_resource_texture_get_info ( xf_texture_info_t* info, xf_texture_h texture );
void xf_resource_buffer_get_info ( xf_buffer_info_t* info, xf_buffer_h buffer );

xf_texture_t* xf_resource_texture_get ( xf_texture_h texture );
xf_multi_texture_t* xf_resource_multi_texture_get ( xf_texture_h texture );
xf_buffer_t* xf_resource_buffer_get ( xf_buffer_h buffer );
xf_multi_buffer_t* xf_resource_multi_buffer_get ( xf_buffer_h buffer );
xf_physical_texture_t* xf_resource_texture_get_physical_texture ( xf_texture_h texture );
xf_physical_texture_t* xf_resource_physical_texture_get ( xf_physical_texture_h texture );

void xf_resource_buffer_map_to_new ( xf_buffer_h buffer, xg_buffer_h xg_handle, xg_buffer_usage_bit_e allowed_usage );
void xf_resource_texture_unmap ( xf_texture_h texture );
void xf_resource_buffer_unmap ( xf_buffer_h buffer );

void xf_resource_texture_update_info ( xf_texture_h texture, const xg_texture_info_t* info );

void xf_resource_texture_add_usage ( xf_texture_h texture, xg_texture_usage_bit_e usage );
void xf_resource_buffer_add_usage ( xf_buffer_h buffer, xg_buffer_usage_bit_e usage );

void xf_resource_texture_state_barrier ( std_stack_t* stack, xf_texture_h texture, xg_texture_view_t view, const xf_texture_execution_state_t* new_state );
void xf_resource_buffer_state_barrier ( std_stack_t* stack, xf_buffer_h buffer, const xf_buffer_execution_state_t* new_state );
void xf_resource_physical_texture_state_barrier ( std_stack_t* stack, xf_physical_texture_h physical_texture_handle, xg_texture_view_t view, const xf_texture_execution_state_t* new_state );

void xf_resource_texture_alias ( xf_texture_h texture, xf_texture_h alias );
void xf_resource_texture_update_external ( xf_texture_h texture );

/*
    Multi texture handle : | multi texture flag | unused | multi texture subtexture index | multi texture index |
                           |          1         |   31   |               16               |         16          |

    Multi textures: the idea here is to have a tool to represent
        - textures that need to hold temporal data over multiple frames, one texture per frame
        - swapchain textures that are backed by multiple textures, again one texture per frame
    The handle returned when creating one is a plain texture handle, this is to allow to use it in place of any other texture
    After executing the graph the multi texture needs to be advanced, so that the next time the handle is used it will reference 
    the "next" texture between the multi textures.
    In the backend, a multi texture handle has a bit set to 1 to represent that it's a multi texture handle. Storage for multi
    textures is separate from that of normal textures. A multi texture just has an array of references to actual normal textures,
    and an index indicating the current active texture. Calling advance on a multi texture simply bumps this index, modulo the
    multi texture count.
    Every xf_resource function that takes a texture should check for the top bit and if set treat the handle as a multi texture
    handle.
*/
xf_texture_h xf_resource_multi_texture_create ( const xf_multi_texture_params_t* params );
xf_texture_h xf_resource_multi_texture_create_from_swapchain ( xg_swapchain_h swapchain );
void xf_resource_multi_texture_advance ( xf_texture_h multi_texture );
void xf_resource_multi_texture_set_index ( xf_texture_h multi_texture, uint32_t index );
xg_swapchain_h xf_resource_multi_texture_get_swapchain ( xf_texture_h texture );
xf_texture_h xf_resource_multi_texture_get_texture ( xf_texture_h multi_texture, int32_t offset );
xf_texture_h xf_resource_multi_texture_get_default ( xf_texture_h multi_texture );

xf_buffer_h xf_resource_multi_buffer_create ( const xf_multi_buffer_params_t* params );
void xf_resource_multi_buffer_advance ( xf_buffer_h multi_buffer );
xf_buffer_h xf_resource_multi_buffer_get_buffer ( xf_buffer_h multi_buffer, int32_t offset );
xf_buffer_h xf_resource_multi_buffer_get_default ( xf_buffer_h multi_buffer );

bool xf_resource_texture_is_multi ( xf_texture_h texture );
bool xf_resource_buffer_is_multi ( xf_buffer_h buffer );

void xf_resource_swapchain_resize ( xf_texture_h swapchain );

void xf_resource_texture_add_ref ( xf_texture_h texture );
void xf_resource_texture_remove_ref ( xf_texture_h texture );
void xf_resource_buffer_add_ref ( xf_buffer_h buffer );
void xf_resource_buffer_remove_ref ( xf_buffer_h buffer );

// --

void xf_resource_destroy_unreferenced ( xg_i* xg, xg_resource_cmd_buffer_h resource_cmd_buffer, xg_resource_cmd_buffer_time_e time );

void xf_resource_physical_texture_map_to_new ( xf_physical_texture_h physical_texture_handle, xg_texture_h xg_handle, const xg_texture_info_t* info );

void xf_resource_texture_bind ( xf_texture_h texture, xf_physical_texture_h physical_texture );
void xf_resource_texture_unbind ( xf_texture_h texture );

xf_physical_texture_h xf_resource_physical_texture_create ( const xf_physical_texture_params_t* params );
void xf_resource_physical_texture_destroy ( xf_physical_texture_h texture );
void xf_resource_physical_texture_add_ref ( xf_physical_texture_h texture );
void xf_resource_physical_texture_remove_ref ( xf_physical_texture_h texture );

xf_texture_h xf_resource_multi_texture_get_base ( xf_texture_h multi_texture );

bool xf_resource_texture_is_depth ( xf_texture_h texture );

uint32_t xf_resource_texture_list ( xf_texture_h* textures, uint32_t capacity );

xf_texture_execution_state_t xf_resource_texture_get_state ( xf_texture_h texture, xg_texture_view_t view );
