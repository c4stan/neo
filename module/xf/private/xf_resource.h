#pragma once

#include <xf.h>

#include <std_array.h>
#include <std_allocator.h>

/*
    Resource flow
        user declares resources trough declare_texture/buffer api, gets a xf resource handle
        user lists resource inputs and outputs when creating a node, using xf resource handles
        at graph build time, before calling the node user callback, an xg resource cmd buffer is used to create the xg resource using the parameters coming from the xf resource declaration
        the create cmd is registered on the resource cmd buffer
        the node user callback is called, the resource gets used
        once all nodes have been processed, the xg (resource) cmd buffers are submitted
        permanent resources are kept alive, next frame it won't be necessary to recreate, just grab the already existing one
*/

/*
    TODO support for multi textures (to support history on prev frame textures, and be able to track state for all swapchain textures)
*/

// Stores what kind of access the graph nodes do on a (sub) resource
// Filled when adding nodes to a graph and never updated after
typedef struct {
    xf_node_h writers[xf_resource_max_writers_m];
    xf_node_h readers[xf_resource_max_readers_m];
    size_t readers_count;
    size_t writers_count;
} xf_resource_dependencies_t;

// Keeps track of the state of a (sub) resource
// Updated during graph execution whenever a new memory barrier is queued up for the resource
typedef struct {
    xg_texture_layout_e layout;
    xg_pipeline_stage_f stage;
    xg_memory_access_f access;
} xf_texture_execution_state_t;

#define xf_texture_execution_state_m( ... ) ( xf_texture_execution_state_t ) { \
    .layout = xg_texture_layout_undefined_m, \
    .stage = xg_pipeline_stage_invalid_m, \
    .access = xg_memory_access_none_m, \
    ##__VA_ARGS__ \
}

typedef struct {
    xg_pipeline_stage_f stage;
    xg_memory_access_f access;
} xf_buffer_execution_state_t;

#define xf_buffer_execution_state_m( ... ) ( xf_buffer_execution_state_t ) { \
    .stage = xg_pipeline_stage_invalid_m, \
    .access = xg_memory_access_none_m, \
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

typedef struct {
    bool is_dirty;
    bool is_user_bind;
    xg_texture_h xg_handle;
    xg_texture_usage_f required_usage;
    xg_texture_usage_f allowed_usage;
    xf_texture_params_t params;

    union {
        xf_resource_dependencies_t shared;
        xf_resource_dependencies_t mips[16]; // TODO make storage external?
        // TODO external hash table to support dynamic view access
    } deps;

    union {
        xf_texture_execution_state_t shared;
        xf_texture_execution_state_t mips[16]; // TODO make storage external?
        // TODO external hash table to support dynamic view access
    } state;

    xf_texture_h alias;
} xf_texture_t;

typedef struct {
    bool is_dirty;
    xg_buffer_h xg_handle;
    xf_buffer_execution_state_t state;
    xf_resource_dependencies_t deps;
    xg_buffer_usage_f required_usage;
    xg_buffer_usage_f allowed_usage;
    xf_buffer_params_t params;
    xf_buffer_h alias;
} xf_buffer_t;

typedef struct {
    xf_multi_texture_params_t params;
    uint32_t index;
    xf_texture_h textures[xf_resource_multi_texture_max_textures_m];
    xf_texture_h alias;
} xf_multi_texture_t;

typedef struct {
    xf_multi_buffer_params_t params;
    uint32_t index;
    xf_buffer_h buffers[xf_resource_multi_buffer_max_buffers_m];
    xf_buffer_h alias;
} xf_multi_buffer_t;

typedef struct {
    xf_buffer_t* buffers_array;
    xf_buffer_t* buffers_freelist;

    xf_multi_buffer_t* multi_buffers_array;
    xf_multi_buffer_t* multi_buffers_freelist;

    xf_texture_t* textures_array;
    xf_texture_t* textures_freelist;

    xf_multi_texture_t* multi_textures_array;
    xf_multi_texture_t* multi_textures_freelist;
} xf_resource_state_t;

void xf_resource_load ( xf_resource_state_t* state );
void xf_resource_reload ( xf_resource_state_t* state );
void xf_resource_unload ( void );

// TODO group some of those setters into a single function that sets everything and locks only once

xf_texture_h xf_resource_texture_declare ( const xf_texture_params_t* params );
xf_buffer_h xf_resource_buffer_declare ( const xf_buffer_params_t* params );

void xf_resource_texture_get_info ( xf_texture_info_t* info, xf_texture_h texture );
void xf_resource_buffer_get_info ( xf_buffer_info_t* info, xf_buffer_h buffer );

//void xf_resource_texture_bind ( xf_texture_h texture_handle, xg_texture_h xg_handle, const xg_texture_info_t* info );

bool xf_resource_texture_is_multi ( xf_texture_h texture );
bool xf_resource_buffer_is_multi ( xf_buffer_h buffer );

xf_texture_t* xf_resource_texture_get ( xf_texture_h texture );
xf_buffer_t* xf_resource_buffer_get ( xf_buffer_h buffer );

// t is used as space to store the result when the texture has per-mip dependencies.
// in that case the dependencies get accumulated on t and a pointer to t is returned.
// t is expected to be initialized when passed in. it is acceptable for it to be non-empty.
const xf_resource_dependencies_t* xf_resource_texture_get_deps ( xf_texture_h texture, xg_texture_view_t view, xf_resource_dependencies_t* t );

void xf_resource_texture_map_to_new ( xf_texture_h texture, xg_texture_h xg_handle );
void xf_resource_buffer_map_to_new ( xf_buffer_h buffer, xg_buffer_h xg_handle );

void xf_resource_texture_update_info ( xf_texture_h texture, const xg_texture_info_t* info );
void xf_resource_texture_set_allowed_usage ( xf_texture_h texture, xg_texture_usage_f usage );

void xf_resource_texture_add_usage ( xf_texture_h texture, xg_texture_usage_f usage );
void xf_resource_buffer_add_usage ( xf_buffer_h buffer, xg_buffer_usage_f usage );

void xf_resource_texture_add_reader ( xf_texture_h texture, xg_texture_view_t view, xf_node_h reader );
void xf_resource_texture_add_writer ( xf_texture_h texture, xg_texture_view_t view, xf_node_h writer );
void xf_resource_buffer_add_reader ( xf_buffer_h buffer, xf_node_h reader );
void xf_resource_buffer_add_writer ( xf_buffer_h buffer, xf_node_h writer );

void xf_resource_texture_set_execution_state ( xf_texture_h texture, xg_texture_view_t view, const xf_texture_execution_state_t* state );
void xf_resource_texture_set_execution_layout ( xf_texture_h texture, xg_texture_view_t view, xg_texture_layout_e layout );
void xf_resource_texture_add_execution_stage ( xf_texture_h texture, xg_texture_view_t view, xg_pipeline_stage_f stage );

void xf_resource_buffer_set_execution_state ( xf_buffer_h buffer, const xf_buffer_execution_state_t* state );
void xf_resource_buffer_add_execution_stage ( xf_buffer_h buffer, xg_pipeline_stage_f stage );

// TODO remove these and nove dirty flag clear into map_to_new?
void xf_resource_texture_set_dirty ( xf_texture_h texture, bool is_dirty );
void xf_resource_buffer_set_dirty ( xf_buffer_h buffer, bool is_dirty );

void xf_resource_texture_refresh_external ( xf_texture_h texture );

/*
    Multi texture handle : | multi texture flag | unused | multi texture subtexture index | multi texture index |
                           |          1         |   31   |               16               |         16          |

    Multi textures: the idea here is to have a tool to represent textures that
        - textures that need to hold temporal data over multiple frames, one texture per frame
        - swapchain textures that are backed by multiple textures, again one texture per frame
    The handle returned when creating one is a plain texture handle, this is to allow to use it in place of any other texture
    After executing the graph the user is supposed to call advance on the handle, so that the next time the handle is used
    it will reference the "next" texture between the multi textures.
    In the backend, a multi texture handle has the bit set to 1 to represent that it's a multi texture handle. Storage for multi
    textures is separate from that of normal textures. A multi texture just has an array of references to actual normal textures,
    and an index indicating the current active texture. Calling advance on a multi texture simply bumps this index, modulo the
    multi texture count.
    Every xf_resource function that takes a texture should check for the top bit and if set treat the handle as a multi texture
    handle.
*/
xf_texture_h xf_resource_multi_texture_declare ( const xf_multi_texture_params_t* params );
void xf_resource_multi_texture_advance ( xf_texture_h multi_texture );
xf_texture_h xf_resource_multi_texture_declare_from_swapchain ( xg_swapchain_h swapchain );

xf_texture_h xf_resource_texture_declare_from_external ( xg_texture_h texture );

xf_texture_h xf_resource_multi_texture_get ( xf_texture_h multi_texture, int32_t offset );

void xf_resource_texture_state_barrier ( std_stack_t* stack, xf_texture_h texture, xg_texture_view_t view, const xf_texture_execution_state_t* new_state );

void xf_resource_buffer_state_barrier ( std_stack_t* stack, xf_buffer_h buffer, const xf_buffer_execution_state_t* new_state );

void xf_resource_texture_alias ( xf_texture_h texture, xf_texture_h alias );
