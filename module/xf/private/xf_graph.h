#pragma once

#include <xf.h>

#include <std_hash.h>
#include <std_queue.h>

#include "xf_resource.h"

/*
    Full rebuild every frame vs rebuild on user request/fraph change only?
        Every frame probably makes the most sense
        Compute workloads in particular can vary significantly between frames
        Makes it easier to schedule occasional setup/one-frame work
        One alternative could be allowing to turn on-off sub-graph or sections of the graph and rebuild onlu what's affected by that
            Likely to get quite complex, and not sure if the gain would be significant (or any)
        Build on every frame first, once that's working evaluate alternatives later

    Nodes as semi stand-alone things that one can reuse and treat like prefabs, vs simple graph-centric nodes management
        Would be cool if I could get a "SSAO node" "prefab" declaration from somewhere and just plug that into my graph
        In practice that means providing the all the enums for slotting the resources in the node params, the user callback and the user args declaration.
        Doesn't really require any special treatment, comes by default.

    Separete create_node and add_node_to_graph calls or all in one?
        In theory having a separate add_node_to_graph means that a node can be part of multiple graphs, meaning that its dependencies can come from multiple
        graphs, meaning that I can't look only inside the graph when determining dependencies but instead must take into consideration all other existing nodes
        Currently the api only has a create_node call, best to keep it that way for now, perhaps expand later.
*/

/*
    Resources
        https://logins.github.io/graphics/2021/05/31/RenderGraphs.html
        https://levelup.gitconnected.com/organizing-gpu-work-with-directed-acyclic-graphs-f3fd5f2c2af3
        https://docs.unrealengine.com/4.26/en-US/ProgrammingAndScripting/Rendering/RenderDependencyGraph
        https://www.gdcvault.com/play/1024656/Advanced-Graphics-Tech-Moving-to
        https://themaister.net/blog/2017/08/15/render-graphs-and-vulkan-a-deep-dive
        https://apoorvaj.io/render-graphs-1

        https://www.jeremyong.com/rendering/2019/06/28/render-graph-optimization-scribbles/

    Node Dependencies
        In order to linearize the graph it's necessary to go from implicit node dependencies (e.g. resource usage) to explicit (e.g. node A depends on node B).
        One way is to start from a 'root' node, and from there recursively define explicit dependencies based on resource usage, where reads depend on previous writes.
        This works as long as there's only one writer per resource. This is rather limiting, and simple situations of write -> read -> write on same resource are
        no longer allowed. Same for multiple nodes using the same render target.
        -One solution to this limitations can be in the form of adding explicit dependencies between nodes, not based on resources, that help solve the case of
        multiple writes, assigning an arbitrary ordering.
        -Another solution is to use multiple user-level resources to refer to the same physical resource. This can work for a write -> read -> write on same resource,
        by simply doing the second write on a new resource and trusting the backend to reuse the same resource. Cases where some of the previous content must be
        preserved need to be explicitly declared however, for example a write -> write on the same resource, where each write acts on a subregion.
        -Another solution is to use the order in which the nodes are added to the graph as a baseline. In this scenario the order of a w->r->w or even a w->w is simply
        determined by the order in which their respective nodes were added to the graph. Partial reordering is still possible to maximize distance between
        dependencies.
        Solution: follow nodes declaration order

    Node execute order vs gpu command submission order
        Nodes get sorted based on their dependencies, but that is only needed in order to determine the sort keys passed to the node callbacks.
        Once the sort has happened, the execute step can iterate over all nodes and spawn a task for each execute, accumulating each node key space on the sort key
        that gets passed as param. Node callbacks can then record commands in parallel using keys in the sort key space they declared, with the param key as base, and
        the xg backend can do the sort later of the commands based on keys.
        For node callbacks to be executed in parallel, it must be required that the callbacks don't modify any state that's shared between the callbacks. This includes
        xg resources state. It follows that it might be best for xg cmd buffer to have an API that's separate from the xg one, and only provide the cmd buffer api
        to the callback.
*/

typedef uint32_t xf_graph_texture_h;
typedef uint32_t xf_graph_buffer_h;

typedef uint64_t xf_node_texture_h;
typedef uint64_t xf_node_buffer_h;

typedef enum {
    //xf_graph_resource_access_read_m,
    //xf_graph_resource_access_write_m,
    xf_graph_resource_access_sampled_m,
    xf_graph_resource_access_uniform_m,
    xf_graph_resource_access_storage_read_m,
    xf_graph_resource_access_copy_read_m,
    xf_graph_resource_access_render_target_m,
    xf_graph_resource_access_depth_target_m,
    xf_graph_resource_access_storage_write_m,
    xf_graph_resource_access_copy_write_m,
    xf_graph_resource_access_invalid_m,
} xf_graph_resource_access_e;

#define xf_graph_resource_access_is_read( a ) ( a <= xf_graph_resource_access_copy_read_m )
#define xf_graph_resource_access_is_write( a ) ( a > xf_graph_resource_access_copy_read_m && a <= xf_graph_resource_access_copy_write_m )

typedef struct {
    xf_node_h node;
    uint32_t resource_idx; // indexes node resources_array
    // TODO specify subresource too here?
} xf_graph_resource_access_t;

#define xf_graph_resource_access_m( ... ) ( xf_graph_resource_access_t ) { \
    .node = xf_null_handle_m, \
    .resource_idx = -1, \
    ##__VA_ARGS__ \
}

typedef struct {
    xf_graph_resource_access_t array[xf_graph_max_nodes_m];
    uint32_t count;
} xf_graph_subresource_dependencies_t;

// Stores what kind of access the graph nodes do on a (sub) resource
// Filled when adding nodes to a graph and never updated after
typedef struct {
    xf_graph_subresource_dependencies_t* subresources;
    uint32_t subresource_count;
} xf_graph_resource_dependencies_t;

#define xf_graph_resource_dependencies_m( ... ) ( xf_graph_resource_dependencies_t ) { \
    .subresources = NULL, \
    .subresource_count = 0, \
    ##__VA_ARGS__ \
}

typedef uint64_t xf_graph_physical_texture_h;

typedef struct {
    xf_physical_texture_h handle;
    xf_graph_resource_dependencies_t dependencies;
} xf_graph_physical_texture_t;

#define xf_graph_physical_texture_m( ... ) ( xf_graph_physical_texture_t ) { \
    .handle = xf_null_handle_m, \
    .dependencies = xf_graph_resource_dependencies_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    int32_t first; // indexes graph nodes_execution_order
    int32_t last[xg_cmd_queue_count_m];
} xf_graph_resource_lifespan_t;

#define xf_graph_resource_lifespan_m( ... ) ( xf_graph_resource_lifespan_t ) { \
    .first = INT32_MAX, \
    .last = { -1, -1, -1 }, \
    ##__VA_ARGS__ \
}

typedef struct {
    xf_texture_h handle;
    xf_graph_physical_texture_h physical_texture_handle;
    xf_graph_resource_lifespan_t lifespan;
    //union {
    //    xf_graph_resource_dependencies_t shared;
    //    xf_graph_resource_dependencies_t mips[16]; // TODO make storage external?
    //    // TODO external hash table to support dynamic view access
    //} deps;
    xf_graph_resource_dependencies_t dependencies;
    // cached from underlying texture
} xf_graph_texture_t;

#define xf_graph_texture_m( ... ) ( xf_graph_texture_t ) { \
    .handle = xf_null_handle_m, \
    .lifespan = xf_graph_resource_lifespan_m(), \
    .dependencies = xf_graph_resource_dependencies_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    xf_buffer_h handle;
    xf_graph_resource_lifespan_t lifespan;
    xf_graph_resource_dependencies_t dependencies;
} xf_graph_buffer_t;

#define xf_graph_buffer_m( ... ) ( xf_graph_buffer_t ) { \
    .handle = xf_null_handle_m, \
    .lifespan = xf_graph_resource_lifespan_m(), \
    .dependencies = xf_graph_resource_dependencies_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    xg_memory_h memory_handle;
    xf_physical_texture_h textures_array[xf_graph_max_textures_m];
    uint32_t textures_count;
} xf_graph_memory_heap_t;

// TODO:
//      segment nodes in execution order by queue
//          for each segment determine its dependencies
//              for each node in segment look at cross queue deps,
//              if node depends on other node outside of segment,
//              add dependency between segments
//
//
typedef struct {
    uint32_t begin; // indexes nodes_execution_order
    uint32_t end;
    xg_cmd_queue_e queue;
    xg_queue_event_h event;
    uint32_t deps[xf_graph_max_nodes_m]; // indexes other segments. list of segments that need to execute directly before this one.
    uint32_t deps_count;
    bool is_depended; // flagged if some other segment depends on this one. means it needs a valid event
} xf_graph_segment_t;

#define xf_graph_segment_m( ... ) ( xf_graph_segment_t ) { \
    .begin = -1, \
    .end = -1, \
    .queue = xg_cmd_queue_invalid_m, \
    .event = xg_null_handle_m, \
    .deps_count = 0, \
    .is_depended = false, \
    ##__VA_ARGS__ \
}

//typedef enum {
//    xf_graph_resource_access_sampled_m,
//    xf_graph_resource_access_uniform_m,
//    xf_graph_resource_access_storage_read_m,
//    xf_graph_resource_access_copy_read_m,
//    xf_graph_resource_access_render_target_m,
//    xf_graph_resource_access_depth_target_m,
//    xf_graph_resource_access_storage_write_m,
//    xf_graph_resource_access_copy_write_m,
//    xf_graph_resource_access_invalid_m,
//} xf_graph_resource_access_e;
//
//#define xf_graph_resource_access_is_read( a ) ( a <= xf_graph_resource_access_copy_read_m )
//#define xf_graph_resource_access_is_write( a ) ( a > xf_graph_resource_access_copy_read_m && a <= xf_graph_resource_access_copy_write_m )

typedef enum {
    xf_node_resource_texture_m,
    xf_node_resource_buffer_m,
    xf_node_resource_invalid_m,
} xf_node_resource_e;

typedef struct {
    xf_node_resource_e type;
    xg_pipeline_stage_bit_e stage;
    xf_graph_resource_access_e access;
    union {
        struct {
            xf_graph_texture_h graph_handle;
            xg_texture_view_t view;
            xg_texture_layout_e layout;
        } texture;
        struct {
            xf_graph_buffer_h graph_handle;
        } buffer;
    };
} xf_node_resource_t;

#define xf_node_resource_m( ... ) ( xf_node_resource_t ) { \
    .type = xf_node_resource_invalid_m, \
    .stage = xg_pipeline_stage_bit_none_m, \
    .access = xf_graph_resource_access_invalid_m, \
    ##__VA_ARGS__ \
}

typedef struct {
    xf_texture_execution_state_t state;
    xf_texture_h texture;
    xg_texture_view_t view;
} xf_graph_texture_transition_t;

#define xf_graph_texture_transition_m( ... ) ( xf_graph_texture_transition_t ) { \
    .state = xf_texture_execution_state_m(), \
    .texture = xf_null_handle_m, \
    .view = xg_texture_view_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    xf_graph_texture_transition_t array[xf_graph_max_nodes_m];
    uint32_t count;
} xf_graph_texture_transitions_t;

#define xf_graph_texture_transitions_m( ... ) { \
    .count = 0, \
    ##__VA_ARGS__ \
}

typedef struct {
    xf_buffer_execution_state_t state;
    xf_buffer_h buffer; // TODO physical buffer
} xf_graph_buffer_transition_t;

#define xf_graph_buffer_transition_m( ... ) ( xf_graph_buffer_transition_t ) { \
    .state = xf_buffer_execution_state_m(), \
    .buffer = xf_null_handle_m, \
    ##__VA_ARGS__ \
}

typedef struct {
    xf_graph_buffer_transition_t array[xf_graph_max_nodes_m];
    uint32_t count;
    uint32_t idx;
} xf_graph_buffer_transitions_t;

typedef struct xf_node_t {
    xf_node_params_t params;
    xf_node_h edges[xf_node_max_node_edges_m]; // outgoing dependency edges
    uint32_t edge_count;
    void* user_alloc;
    bool enabled;
    //tk_workload_h cpu_workload;
    xg_renderpass_h renderpass;
    struct {
        xg_render_textures_layout_t render_textures;
        uint32_t resolution_x;
        uint32_t resolution_y;
    } renderpass_params;
    xf_node_h next_nodes[xf_graph_max_nodes_m]; // nodes that receive an arc starting from this node. must be executed after this node
    uint32_t next_nodes_count;
    xf_node_h prev_nodes[xf_graph_max_nodes_m]; // nodes that have an arc ending on this node. must be executed before this node
    uint32_t prev_nodes_count;
    uint32_t execution_order; // indexes graph nodes_execution_order
    uint32_t segment; // indexes graph segments_array

    xf_node_resource_t resources_array[xf_node_max_textures_m + xf_node_max_buffers_m];
    uint32_t resources_count;
    uint32_t textures_array[xf_node_max_textures_m];
    uint32_t textures_count;
    uint32_t buffers_array[xf_node_max_buffers_m];
    uint32_t buffers_count;

    xf_graph_texture_transitions_t texture_transitions;
    xf_graph_texture_transitions_t texture_acquires; // TODO change type, remove unused fields
    xf_graph_texture_transitions_t texture_releases; // TODO change type, remove unused fields
    xf_graph_buffer_transitions_t buffer_transitions;
    xf_graph_buffer_transitions_t buffer_releases;
} xf_node_t;

typedef struct {
    xg_query_pool_h pool;
    xg_workload_h workload;
} xf_graph_query_context_t;

typedef struct {
    xf_graph_params_t params;

    xf_node_t nodes_array[xf_graph_max_nodes_m];
    uint32_t nodes_execution_order[xf_graph_max_nodes_m]; // indexes nodes, sorted by execution order
    uint32_t nodes_count;
    bool is_finalized;
    bool is_built;
    bool is_invalidated;

    xf_graph_texture_t textures_array[xf_graph_max_textures_m];
    xf_graph_buffer_t buffers_array[xf_graph_max_buffers_m];
    uint32_t textures_count;
    uint32_t buffers_count;

    xf_graph_physical_texture_t physical_textures_array[xf_graph_max_textures_m];
    uint32_t physical_textures_count;

    uint32_t multi_textures_array[xf_graph_max_multi_textures_m]; // indexes textures_array
    uint32_t multi_textures_count;
    //uint64_t multi_textures_hashes[xf_graph_max_multi_textures_m];
    //std_hash_set_t multi_textures_hash_set;
    
    // queue[n][q] stores the idx of the predecessor of node n for queue type q. All indices are to nodes_execution_order. defaults to -1
    int32_t cross_queue_node_deps[xf_graph_max_nodes_m][xg_cmd_queue_count_m];

    uint64_t texture_hashes[xf_graph_max_textures_m * 2];
    uint64_t texture_values[xf_graph_max_textures_m];
    std_hash_map_t textures_map;
    uint64_t buffer_hashes[xf_graph_max_buffers_m * 2];
    uint64_t buffer_values[xf_graph_max_buffers_m];
    std_hash_map_t buffers_map;

    xf_graph_memory_heap_t heap; // TODO one per mem type?
    uint32_t owned_textures_array[32]; // indexes textures_array - TODO size
    uint32_t owned_textures_count;

    xf_graph_segment_t segments_array[xf_graph_max_nodes_m];
    uint32_t segments_count;

    std_virtual_stack_t resource_dependencies_allocator;
    std_virtual_stack_t physical_resource_dependencies_allocator;

    xf_graph_query_context_t query_contexts_array[16];
    std_ring_t query_contexts_ring;
    uint64_t latest_timings[xf_graph_max_nodes_m];
} xf_graph_t;

typedef struct {
    xf_graph_t* graphs_array;
    xf_graph_t* graphs_freelist;
    uint64_t* graphs_bitset;
} xf_graph_state_t;

void xf_graph_load ( xf_graph_state_t* state );
void xf_graph_reload ( xf_graph_state_t* state );
void xf_graph_unload ( void );

xf_graph_h xf_graph_create ( const xf_graph_params_t* params );
xf_node_h xf_graph_node_create ( xf_graph_h graph, const xf_node_params_t* params );
void xf_graph_invalidate ( xf_graph_h graph, xg_workload_h workload );
void xf_graph_finalize ( xf_graph_h graph );
uint64_t xf_graph_build ( xf_graph_h graph, xg_workload_h workload, uint64_t key );
uint64_t xf_graph_execute ( xf_graph_h graph, xg_workload_h xg_workload, uint64_t base_key );
void xf_graph_advance_multi_textures ( xf_graph_h graph );
void xf_graph_destroy ( xf_graph_h graph, xg_workload_h workload );

void xf_graph_get_info ( xf_graph_info_t* info, xf_graph_h graph );
void xf_graph_get_node_info ( xf_node_info_t* info, xf_graph_h graph, xf_node_h node );

void xf_graph_debug_print ( xf_graph_h graph );

void xf_graph_node_set_enabled ( xf_graph_h graph, xf_node_h node, bool enabled );
void xf_graph_node_enable ( xf_graph_h graph, xf_node_h node );
void xf_graph_node_disable ( xf_graph_h graph, xf_node_h node );

const uint64_t* xf_graph_get_timings ( xf_graph_h graph );
