#pragma once

#include <std_module.h>

#include <xg.h>
#include <xi.h>

#define xf_module_name_m xf
std_module_export_m void* xf_load ( void* );
std_module_export_m void xf_reload ( void*, void* );
std_module_export_m void xf_unload ( void );

/*
    XF is supposed to provide an API to create work graphs defined by nodes and their resource dependencies, instead of direct connections between nodes
    Mainly (initially?) targets GPU work but the idea of expanding it to CPU work is interesting. Might even have multiple graphs (some rendering related, some cpu/sim only) that get run with separate frequencies and have some kind of middle layer for passing data in between.

    GPU
        Each node lists resources input and output
        Sort nodes based on dependencies, and assign a key to each node based on that
        Actual nodes execution can happen in arbitrary order, should follow ordering implied by CPU resources dependencies if any is present
        Aliasing
            Aliasing can be implemented at resource level, e.g. aliasing 2 textures with same format and size, and at memory level, by just sharing the same memory with multiple separate resources over time
        Compute
            Should compute jobs (e.g. skinning) be run all in one node (e.g. compute skinning node) or have one node per dispatch?
                Most likely the 1st makes more sense
            Can complex compute use cases be supported?
                Example: Need to schedule a render pass that uses a dynamic number of resources (e.g. one dispatch per instance of a specific object present in game)
                    need to have one pre-execute (setup?) callback per node that all get called before execute calls start and declare resources there?
                        means that nodes need to be re-scheduled and sorted every frame instead of just once

            blend -> skin -> cloth
            blend -> skin -> cloth
            ...

            blend pass -> skin pass -> cloth pass ?
                implies that all blends have to be finished before starting skinning, and so on
                    not necessary, every dep. chain is independent
            leave individual dispatches separate
*/

/*
    early ideas re. new module that works as an interface between client rendering and non rendering code and allows client rendering code
    to access all renderables that match a certain set of flags
    basically boils down to a database of <rendering payload, flags> that is queried based on flags and needs to return the payloads
    most simple implementation can be just that, an array of <payload, flags> entries that gets optionally sorted every once in a while,
    and just do a linear search to solve a query and return an array of payloads, each pointing to the actual data on some external storage,
    which could be a block based allocator or anything else based on the actual allocation pattern.

    the above solution would be a very thin layer that allows for querying payloads based on flags, but a wider solution that looks more like
    a classic ECS is also possible. in this case components would hold these flags (possibly in SoA style to avoid paying the cost for the
    weight of the whole component when resolving queries). however a classical ECS tipically allows to query for entities having a set of
    required components. It could be made that there are multiple component types, potentially one per flag, and so querying for flags
    translates into querying for components (not sure how viable this is, but doesn't sound that bad).

    either way, a way to probably optimize the querying is doing "deferred queries" where first all queries for the current frame are entered
    and then they're resolved all at once, and finally the results can be consumed by the clients.
*/

typedef uint64_t xf_resource_h;
typedef uint64_t xf_texture_h;
typedef uint64_t xf_buffer_h;
typedef uint64_t xf_node_h;
typedef uint64_t xf_graph_h;

#define xf_null_handle_m UINT64_MAX

// Dependencies
typedef struct {
    xf_texture_h texture;
    xg_texture_view_t view;
    xg_shading_stage_e shading_stage;
} xf_node_shader_texture_dependency_t;

#define xf_shader_texture_dependency_m( _texture, _view, _shading_stage ) ( xf_node_shader_texture_dependency_t ) { \
    .shading_stage = _shading_stage, \
    .texture = _texture, \
    .view = _view, \
}
#define xf_default_shader_texture_dependency_m xf_shader_texture_dependency_m ( xf_null_handle_m, xg_default_texture_view_m, xg_shading_stage_null_m )

#define xf_sampled_texture_dependency_m( _texture, _shading_stage ) xf_shader_texture_dependency_m ( _texture, xg_default_texture_view_m, _shading_stage )
#define xf_storage_texture_dependency_m( _shading_stage, _texture, _view ) xf_shader_texture_dependency_m ( _texture, _view, _shading_stage )

typedef struct {
    xf_texture_h texture;
    xg_texture_view_t view;
} xf_node_copy_texture_dependency_t;

#define xf_copy_texture_dependency_m( _texture, _view ) ( xf_node_copy_texture_dependency_t ) { .texture = _texture, .view = _view }
#define xf_default_copy_texture_dependency_m xf_copy_texture_dependency_m( xf_null_handle_m, xg_default_texture_view_m )

typedef struct {
    xf_texture_h texture;
    xg_texture_view_t view;
} xf_node_render_target_dependency_t;

#define xf_render_target_dependency_m( _texture, _view ) ( xf_node_render_target_dependency_t ) { .texture = _texture, .view = _view }
#define xf_default_render_target_dependency_m xf_render_target_dependency_m( xf_null_handle_m, xg_default_texture_view_m )

typedef struct {
    xf_buffer_h buffer;
    xg_shading_stage_e shading_stage;
} xf_buffer_dependency_t;

#define xf_buffer_dependency_m( ... ) ( xf_buffer_dependency_t ) { \
    .buffer = xg_null_handle_m, \
    .shading_stage = xg_shading_stage_null_m, \
    ##__VA_ARGS__ \
}

// Resources
typedef struct {
    xg_texture_h texture;
    xg_texture_view_t view;
    xg_texture_layout_e layout;
} xf_shader_texture_resource_t;

#define xf_shader_texture_binding_m( _resource, _register ) ( xg_texture_resource_binding_t ) { \
    .texture = _resource.texture, \
    .shader_register = _register, \
    .layout = _resource.layout, \
    .view = _resource.view, \
}

typedef struct {
    xg_texture_h texture;
    xg_texture_view_t view;
} xf_copy_texture_resource_t;

#define xf_copy_texture_resource_m( _resource ) ( xg_texture_copy_resource_t ) { \
    .texture = _resource.texture, \
    .mip_base = _resource.view.mip_base, \
    .array_base = _resource.view.array_base, \
}

typedef struct {
    xg_texture_h texture;
    xg_texture_view_t view;
} xf_render_target_resource_t;

#define xf_render_target_binding_m( _resource ) ( xg_render_target_binding_t ) { \
    .texture = _resource.texture, \
    .view = _resource.view, \
}

#define xf_render_target_binding_m( _resource ) ( xg_render_target_binding_t ) { \
    .texture = _resource.texture, \
    .view = _resource.view, \
}

// TODO provide xg bindings directly?
typedef struct {
    xf_shader_texture_resource_t shader_texture_reads[xf_node_max_shader_texture_reads_m];
    xf_shader_texture_resource_t shader_texture_writes[xf_node_max_shader_texture_writes_m];
    xg_buffer_h shader_buffer_reads[xf_node_max_shader_buffer_reads_m];
    xg_buffer_h shader_buffer_writes[xf_node_max_shader_buffer_writes_m];

    xf_copy_texture_resource_t copy_texture_reads[xf_node_max_copy_texture_reads_m];
    xf_copy_texture_resource_t copy_texture_writes[xf_node_max_copy_texture_writes_m];
    xg_buffer_h copy_buffer_reads[xf_node_max_copy_buffer_reads_m];
    xg_buffer_h copy_buffer_writes[xf_node_max_copy_buffer_writes_m];

    xf_render_target_resource_t render_targets[xf_node_max_render_targets_m];
    xg_texture_h depth_stencil_target;
} xf_node_io_t;

typedef struct {
    const xf_node_io_t* io;
    xg_device_h device;
    xg_cmd_buffer_h cmd_buffer;
    xg_resource_cmd_buffer_h resource_cmd_buffer;
    xg_workload_h workload;
    xg_pipeline_state_h pipeline_state;
    uint64_t base_key;
} xf_node_execute_args_t;

typedef void ( xf_node_execute_f ) ( const xf_node_execute_args_t* node_args, void* user_args );

#define xf_node_pass_f_m(name) void name ( const xf_node_execute_args_t* node_args, void* user_args )

typedef enum {
    xf_node_passthrough_mode_ignore_m,
    xf_node_passthrough_mode_clear_m,
    xf_node_passthrough_mode_alias_m,
    xf_node_passthrough_mode_copy_m,
    // xf_node_passthrough_mode_execute_flag_m, // todo: call execute anyway and pass a flag in node_args indicating that it's a passthrough
} xf_node_passthrough_mode_e;

typedef struct {
    xf_node_passthrough_mode_e mode;
    union {
        xg_color_clear_t clear;
        xf_texture_h alias;
        xf_node_copy_texture_dependency_t copy_source;
    };
} xf_node_render_target_passthrough_t;

#define xf_node_render_target_passthrough_m( ... ) ( xf_node_render_target_passthrough_t ) { \
    .mode = xf_node_passthrough_mode_ignore_m, \
    .clear = xg_default_color_clear_m, \
    ##__VA_ARGS__ \
}

#define xf_node_default_render_target_passthrough_m xf_node_render_target_passthrough_m()

typedef struct {
    bool enable;
    xf_node_render_target_passthrough_t render_targets[xf_node_max_render_targets_m];
} xf_node_passthrough_params_t;

#define xf_node_passthrough_params_m( ... ) ( xf_node_passthrough_params_t ) { \
    .enable = false, \
    .render_targets = { [0 ... xf_node_max_render_targets_m - 1] = xf_node_default_render_target_passthrough_m }, \
    ##__VA_ARGS__ \
}

#define xf_node_default_passthrough_params_m xf_node_passthrough_params_m()

typedef struct {
    // IO Dependency
    //      instead of this huge list of arrays of resources (one per resource type) have one single array that takes <resource type, resource> ?
    //      optionally resort it into a list like the one below in the backend if it makes things simpler
    //      shader resources that take both a resource and a shading stage can probably be split into different types per shading stage
    //      problem: how to expose the resources inside of the execute callback? buffers layout allows for easy access through enums
    //      leave list of arrays for now
    //
    //      maybe make it a null terminated list? so that user can just zero-initialize the struct and push resources using enums
    //      no need to adjust the resource count manually. but that wouldn't work when using UINT64_MAX as null handle...
    //      either provide a macro to initialize this struct (memcpy null handle to all arrays) or fixup handles to use 0 as null
    //      might be expensive to null intialize all the arrays? i guess workaround is to manually null initialize only the first unused slot
    //      also probably doesn't make a difference...
    //      another alternatice might be to provide macros to push all resources of one type as once, this way null can be appended automatically
    //      at the end of the list. api would need to provide macro overload for all resource counts. probably messy and wont look nice but could work
    //      for now just leave explicit counts and get it to work
    //
    xf_node_shader_texture_dependency_t shader_texture_reads[xf_node_max_shader_texture_reads_m];
    size_t shader_texture_reads_count;
    xf_buffer_dependency_t shader_buffer_reads[xf_node_max_shader_buffer_reads_m];
    size_t shader_buffer_reads_count;

    xf_node_shader_texture_dependency_t shader_texture_writes[xf_node_max_shader_texture_writes_m];
    size_t shader_texture_writes_count;
    xf_buffer_dependency_t shader_buffer_writes[xf_node_max_shader_buffer_writes_m];
    size_t shader_buffer_writes_count;

    xf_node_copy_texture_dependency_t copy_texture_reads[xf_node_max_copy_texture_reads_m];
    size_t copy_texture_reads_count;
    xf_buffer_h copy_buffer_reads[xf_node_max_copy_buffer_reads_m];
    size_t copy_buffer_reads_count;

    xf_node_copy_texture_dependency_t copy_texture_writes[xf_node_max_copy_texture_writes_m];
    size_t copy_texture_writes_count;
    xf_buffer_h copy_buffer_writes[xf_node_max_copy_buffer_writes_m];
    size_t copy_buffer_writes_count;

    xf_node_render_target_dependency_t render_targets[xf_node_max_render_targets_m];
    size_t render_targets_count;
    xf_texture_h depth_stencil_target;

    xf_texture_h presentable_texture;

    // TODO remove? keep? currently unused
    xf_node_h node_dependencies[xf_node_max_node_dependencies_m];
    size_t node_dependencies_count;

    xf_node_passthrough_params_t passthrough;

    // At execution time the pass is given a base key that can be used for render (xg) submission.
    // Here is specified how many key values should be reserved for this node, starting at that base key.
    uint64_t key_space_size;

    // User routine and args
    xf_node_execute_f* execute_routine;
    std_buffer_t user_args;
    bool copy_args;

    char debug_name[xf_debug_name_size_m];
    uint32_t debug_color;
} xf_node_params_t;

#define xf_node_params_m( ... ) ( xf_node_params_t ) { \
    .shader_texture_reads = { [0 ... xf_node_max_shader_texture_reads_m - 1] = xf_default_shader_texture_dependency_m }, \
    .shader_texture_reads_count = 0, \
    .shader_buffer_reads = { [0 ... xf_node_max_shader_buffer_reads_m - 1] = xf_buffer_dependency_m() }, \
    .shader_buffer_reads_count = 0, \
    \
    .shader_texture_writes = { [0 ... xf_node_max_shader_texture_writes_m - 1] = xf_default_shader_texture_dependency_m }, \
    .shader_texture_writes_count = 0, \
    .shader_buffer_writes = { [0 ... xf_node_max_shader_buffer_writes_m - 1] = xf_buffer_dependency_m() }, \
    .shader_buffer_writes_count = 0, \
    \
    .copy_texture_reads = { [0 ... xf_node_max_copy_texture_reads_m - 1] = xf_default_copy_texture_dependency_m }, \
    .copy_texture_reads_count = 0, \
    .copy_buffer_reads = { [0 ... xf_node_max_copy_buffer_reads_m - 1] = xf_null_handle_m }, \
    .copy_buffer_reads_count = 0, \
    \
    .copy_texture_writes = { [0 ... xf_node_max_copy_texture_writes_m - 1] = xf_default_copy_texture_dependency_m }, \
    .copy_texture_writes_count = 0, \
    .copy_buffer_writes = { [0 ... xf_node_max_copy_buffer_writes_m - 1] = xf_null_handle_m }, \
    .copy_buffer_writes_count = 0,\
    \
    .render_targets = { [0 ... xf_node_max_render_targets_m - 1] = xf_default_render_target_dependency_m }, \
    .render_targets_count = 0, \
    .depth_stencil_target = xf_null_handle_m, \
    .presentable_texture = xf_null_handle_m, \
    \
    .node_dependencies = { [0 ... xf_node_max_node_dependencies_m - 1] = xf_null_handle_m }, \
    .node_dependencies_count = 0, \
    \
    .passthrough = xf_node_default_passthrough_params_m, \
    \
    .key_space_size = 0, \
    \
    .execute_routine = NULL, \
    .user_args = std_null_buffer_m, \
    .copy_args = true, \
    \
    .debug_name = {0}, \
    .debug_color = xg_debug_region_color_none_m, \
    ##__VA_ARGS__ \
}

#define xf_default_node_params_m xf_node_params_m()

typedef struct {
    size_t width;
    size_t height;
    size_t depth;
    size_t mip_levels;
    size_t array_layers;
    xg_texture_dimension_e dimension;
    xg_format_e format;
    xg_sample_count_e samples_per_pixel;
    bool allow_aliasing;
    char debug_name[xf_debug_name_size_m];
    bool clear_on_create;
    union {
        xg_color_clear_t color;
        xg_depth_stencil_clear_t depth_stencil;
    } clear;
    xg_texture_view_access_e view_access;
} xf_texture_params_t;

#define xf_texture_params_m( ... ) ( xf_texture_params_t ) { \
    .width = 0, \
    .height = 0, \
    .depth = 1, \
    .mip_levels = 1, \
    .array_layers = 1, \
    .dimension = xg_texture_dimension_2d_m, \
    .format = xg_format_undefined_m, \
    .samples_per_pixel = xg_sample_count_1_m, \
    .allow_aliasing = false, \
    .debug_name = {0}, \
    .clear_on_create = false, \
    .view_access = xg_texture_view_access_default_only_m, \
    ##__VA_ARGS__ \
}
#define xf_default_texture_params_m ( xf_texture_params_t ) xf_texture_params_m()

typedef struct {
    size_t size;
    bool allow_aliasing;
    char debug_name[xf_debug_name_size_m];
} xf_buffer_params_t;

#define xf_buffer_params_m( ... ) ( xf_buffer_params_t ) { \
    .size = 0, \
    .allow_aliasing = false, \
    .debug_name = {0}, \
}

typedef struct {
    xf_texture_params_t texture;
    uint32_t multi_texture_count;
} xf_multi_texture_params_t;

#define xf_multi_texture_params_m( ... ) ( xf_multi_texture_params_t ) { \
    .texture = xf_default_texture_params_m, \
    .multi_texture_count = 2, \
    ##__VA_ARGS__ \
}
#define xf_default_multi_texture_params_m xf_multi_texture_params_m()

typedef struct {
    xf_buffer_params_t buffer;
    uint32_t multi_buffer_count;
} xf_multi_buffer_params_t;

#define xf_multi_buffer_params_m( ... ) ( xf_multi_buffer_params_t ) { \
    .buffer = xf_buffer_params_m(), \
    .multi_buffer_count = 2, \
}

typedef struct {
    size_t width;
    size_t height;
    size_t depth;
    size_t mip_levels;
    size_t array_layers;
    xg_texture_dimension_e dimension;
    xg_format_e format;
    xg_sample_count_e sample_count;
    bool allows_aliasing;
    const char* debug_name;
} xf_texture_info_t;

typedef struct {
    uint64_t size;
    bool allows_aliasing;
    const char* debug_name;
} xf_buffer_info_t;

typedef struct {
    xg_device_h device;
    xg_swapchain_h swapchain;
} xf_graph_info_t;

typedef struct {
    bool enabled;
    bool passthrough;
    char debug_name[xf_debug_name_size_m];
} xf_node_info_t;

typedef struct {
    xf_texture_h ( *declare_texture ) ( const xf_texture_params_t* params );
    xf_buffer_h ( *declare_buffer ) ( const xf_buffer_params_t* params );
    xf_texture_h ( *declare_multi_texture ) ( const xf_multi_texture_params_t* params );
    //xf_buffer_h ( *declare_multi_buffer ) ( const xf_multi_buffer_params_t* params );

    xf_graph_h ( *create_graph ) ( xg_device_h device, xg_swapchain_h swapchain );
    xf_node_h ( *create_node ) ( xf_graph_h graph, const xf_node_params_t* params );
    //void ( *build_graph ) ( xf_graph_h graph );
    void ( *execute_graph ) ( xf_graph_h graph, xg_workload_h workload );

    void ( *disable_node ) ( xf_node_h node );
    void ( *enable_node ) ( xf_node_h node );

    void ( *advance_multi_texture ) ( xf_texture_h multi_texture );
    //void ( *advance_multi_buffer ) ( xf_multi_buffer_h multi_buffer );
    xf_texture_h ( *get_multi_texture ) ( xf_texture_h multi_texture, int32_t offset );

    xf_texture_h ( *multi_texture_from_swapchain ) ( xg_swapchain_h swapchain );
    xf_texture_h ( *texture_from_external ) ( xf_texture_h texture );
    void ( *refresh_external_texture ) ( xf_texture_h texture );

    void ( *get_texture_info ) ( xf_texture_info_t* info, xf_texture_h texture );
    void ( *get_graph_info ) ( xf_graph_info_t* info, xf_graph_h graph );
    void ( *get_node_info ) ( xf_node_info_t* info, xf_node_h node );

    void ( *debug_print_graph ) ( xf_graph_h graph );
    void ( *debug_ui_graph ) ( xi_i* xi, xi_workload_h workload, xf_graph_h graph ); // adding a xf->xi dependency here kind of sucks...
} xf_i;
