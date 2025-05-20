#pragma once

#include <std_module.h>

#include <xg.h>
#include <xs.h>

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
            Aliasing can be implemented at resource level, e.g. aliasing 2 textures with same format and size, or at memory level, by binding multiple resources to the same or overlapping memory segments and issuing a barrier when moving from using one resource to the other, see https://asawicki.info/articles/memory_management_vulkan_direct3d_12.php5 "Aliasing"
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
typedef uint64_t xf_node_h; // TODO u32?
typedef uint64_t xf_graph_h;

#define xf_null_handle_m UINT64_MAX

// Dependencies
typedef struct {
    xf_texture_h texture;
    xg_texture_view_t view;
    xg_pipeline_stage_bit_e stage;
} xf_shader_texture_dependency_t;

#define xf_shader_texture_dependency_m( ... ) ( xf_shader_texture_dependency_t ) { \
    .stage = xg_pipeline_stage_bit_none_m, \
    .texture = xf_null_handle_m, \
    .view = xg_texture_view_m(), \
    ##__VA_ARGS__ \
}

#define xf_compute_texture_dependency_m( ... ) xf_shader_texture_dependency_m ( .stage = xg_pipeline_stage_bit_compute_shader_m, ##__VA_ARGS__ )
#define xf_fragment_texture_dependency_m( ... ) xf_shader_texture_dependency_m ( .stage = xg_pipeline_stage_bit_fragment_shader_m, ##__VA_ARGS__ )

typedef struct {
    xf_texture_h texture;
    xg_texture_view_t view;
} xf_copy_texture_dependency_t;

#define xf_copy_texture_dependency_m( ... ) ( xf_copy_texture_dependency_t ) { \
    .texture = xf_null_handle_m, \
    .view = xg_texture_view_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    xf_texture_h texture;
    xg_texture_view_t view;
} xf_render_target_dependency_t;

#define xf_render_target_dependency_m( ... ) ( xf_render_target_dependency_t ) { \
    .texture = xf_null_handle_m, \
    .view = xg_texture_view_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    xf_buffer_h buffer; // TODO range ( need to add support to xf_resource? )
    xg_pipeline_stage_bit_e stage;
} xf_shader_buffer_dependency_t;

#define xf_shader_buffer_dependency_m( ... ) ( xf_shader_buffer_dependency_t ) { \
    .buffer = xg_null_handle_m, \
    .stage = xg_pipeline_stage_bit_none_m, \
    ##__VA_ARGS__ \
}

#define xf_compute_buffer_dependency_m( ... ) xf_shader_buffer_dependency_m ( .stage = xg_pipeline_stage_bit_compute_shader_m, ##__VA_ARGS__ )

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

typedef struct {
    xf_shader_texture_resource_t sampled_textures[xf_node_max_sampled_textures_m];
    xf_shader_texture_resource_t storage_texture_reads[xf_node_max_storage_texture_reads_m];
    xf_shader_texture_resource_t storage_texture_writes[xf_node_max_storage_texture_writes_m];
    xg_buffer_h uniform_buffers[xf_node_max_uniform_buffers_m]; // TODO buffer_range ( need to add support to xf_resource first? )
    xg_buffer_h storage_buffer_reads[xf_node_max_storage_buffer_reads_m];
    xg_buffer_h storage_buffer_writes[xf_node_max_storage_buffer_writes_m];

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
    //xg_pipeline_state_h pipeline_state;
    xg_renderpass_h renderpass;
    uint64_t base_key;
    const char* debug_name;
} xf_node_execute_args_t;

typedef void ( xf_node_execute_f ) ( const xf_node_execute_args_t* node_args, void* user_args );

#define xf_node_pass_f_m(name) void name ( const xf_node_execute_args_t* node_args, void* user_args )

typedef enum {
    xf_passthrough_mode_ignore_m,
    xf_passthrough_mode_clear_m,
    //xf_passthrough_mode_alias_m,
    xf_passthrough_mode_copy_m,
    // xf_passthrough_mode_execute_flag_m, // todo: call execute anyway and pass a flag in node_args indicating that it's a passthrough
} xf_passthrough_mode_e;

typedef struct {
    xf_passthrough_mode_e mode;
    union {
        xg_color_clear_t clear;
        //xf_texture_h alias;
        xf_copy_texture_dependency_t copy_source;
    };
} xf_texture_passthrough_t;

#define xf_texture_passthrough_m( ... ) ( xf_texture_passthrough_t ) { \
    .mode = xf_passthrough_mode_ignore_m, \
    .clear = xg_color_clear_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    bool enable;
    xf_texture_passthrough_t render_targets[xf_node_max_render_targets_m];
    xf_texture_passthrough_t storage_texture_writes[xf_node_max_storage_texture_writes_m];
} xf_node_passthrough_params_t;

// TODO add depth_stencil?
#define xf_node_passthrough_params_m( ... ) ( xf_node_passthrough_params_t ) { \
    .enable = false, \
    .render_targets = { [0 ... xf_node_max_render_targets_m - 1] = xf_texture_passthrough_m() }, \
    .storage_texture_writes = { [0 ... xf_node_max_storage_texture_writes_m - 1] = xf_texture_passthrough_m() }, \
    ##__VA_ARGS__ \
}

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
    xf_shader_texture_dependency_t sampled_textures[xf_node_max_sampled_textures_m];
    uint32_t sampled_textures_count;
    xf_shader_texture_dependency_t storage_texture_reads[xf_node_max_storage_texture_reads_m];
    uint32_t storage_texture_reads_count;
    xf_shader_texture_dependency_t storage_texture_writes[xf_node_max_storage_texture_writes_m];
    uint32_t storage_texture_writes_count;

    xf_shader_buffer_dependency_t uniform_buffers[xf_node_max_uniform_buffers_m];
    uint32_t uniform_buffers_count;
    xf_shader_buffer_dependency_t storage_buffer_reads[xf_node_max_storage_buffer_reads_m];
    uint32_t storage_buffer_reads_count;
    xf_shader_buffer_dependency_t storage_buffer_writes[xf_node_max_storage_buffer_writes_m];
    uint32_t storage_buffer_writes_count;

    xf_copy_texture_dependency_t copy_texture_reads[xf_node_max_copy_texture_reads_m];
    size_t copy_texture_reads_count;
    xf_copy_texture_dependency_t copy_texture_writes[xf_node_max_copy_texture_writes_m];
    size_t copy_texture_writes_count;

    xf_buffer_h copy_buffer_reads[xf_node_max_copy_buffer_reads_m];
    size_t copy_buffer_reads_count;
    xf_buffer_h copy_buffer_writes[xf_node_max_copy_buffer_writes_m];
    size_t copy_buffer_writes_count;

    xf_render_target_dependency_t render_targets[xf_node_max_render_targets_m];
    size_t render_targets_count;
    xf_texture_h depth_stencil_target;

    xf_texture_h presentable_texture;

#if 0
    // TODO remove? keep? currently unused
    xf_node_h node_dependencies[xf_node_max_node_dependencies_m];
    size_t node_dependencies_count;
#endif
} xf_node_resource_params_t;

#define xf_node_resource_params_m( ... ) ( xf_node_resource_params_t ) { \
    .sampled_textures = { [0 ... xf_node_max_sampled_textures_m - 1] = xf_shader_texture_dependency_m() }, \
    .sampled_textures_count = 0, \
    .storage_texture_reads = { [0 ... xf_node_max_storage_texture_reads_m - 1] = xf_shader_texture_dependency_m() }, \
    .storage_texture_reads_count = 0, \
    .storage_texture_writes = { [0 ... xf_node_max_storage_texture_writes_m - 1] = xf_shader_texture_dependency_m() }, \
    .storage_texture_writes_count = 0, \
    \
    .uniform_buffers = { [0 ... xf_node_max_uniform_buffers_m - 1] = xf_shader_buffer_dependency_m() }, \
    .uniform_buffers_count = 0, \
    .storage_buffer_reads = { [0 ... xf_node_max_storage_buffer_reads_m - 1] = xf_shader_buffer_dependency_m() }, \
    .storage_buffer_reads_count = 0, \
    .storage_buffer_writes = { [0 ... xf_node_max_storage_buffer_writes_m - 1] = xf_shader_buffer_dependency_m() }, \
    .storage_buffer_writes_count = 0, \
    \
    .copy_texture_reads = { [0 ... xf_node_max_copy_texture_reads_m - 1] = xf_copy_texture_dependency_m() }, \
    .copy_texture_reads_count = 0, \
    .copy_texture_writes = { [0 ... xf_node_max_copy_texture_writes_m - 1] = xf_copy_texture_dependency_m() }, \
    .copy_texture_writes_count = 0, \
    \
    .copy_buffer_reads = { [0 ... xf_node_max_copy_buffer_reads_m - 1] = xf_null_handle_m }, \
    .copy_buffer_reads_count = 0, \
    .copy_buffer_writes = { [0 ... xf_node_max_copy_buffer_writes_m - 1] = xf_null_handle_m }, \
    .copy_buffer_writes_count = 0,\
    \
    .render_targets = { [0 ... xf_node_max_render_targets_m - 1] = xf_render_target_dependency_m() }, \
    .render_targets_count = 0, \
    .depth_stencil_target = xf_null_handle_m, \
    \
    .presentable_texture = xf_null_handle_m, \
    ##__VA_ARGS__ \
}

typedef struct {
    // At execution time the pass is given a base key that can be used for render (xg) submission.
    // Here is specified how many key values should be reserved for this node, starting at that base key.
    uint64_t key_space_size;
    xf_node_execute_f* routine;
    std_buffer_t user_args;
    bool copy_args;
    bool auto_renderpass;
} xf_node_custom_pass_params_t;

#define xf_node_custom_pass_params_m( ... ) ( xf_node_custom_pass_params_t ) { \
    .key_space_size = 0, \
    .routine = NULL, \
    .user_args = std_null_buffer_m, \
    .copy_args = true, \
    .auto_renderpass = false, \
    ##__VA_ARGS__ \
}

typedef struct {
    xg_compute_pipeline_state_h pipeline;
    uint32_t workgroup_count[3];
    std_buffer_t uniform_data;
    bool copy_uniform_data;
    xg_sampler_h samplers[xg_pipeline_resource_max_samplers_per_set_m];
    uint32_t samplers_count;
} xf_node_compute_pass_params_t;

#define xf_node_compute_pass_params_m( ... ) ( xf_node_compute_pass_params_t ) { \
    .pipeline = xg_null_handle_m, \
    .workgroup_count = { 1, 1, 1 }, \
    .uniform_data = std_null_buffer_m, \
    .copy_uniform_data = true, \
    .samplers = { [0 ... xg_pipeline_resource_max_samplers_per_set_m-1] = xg_null_handle_m }, \
    .samplers_count = 0, \
    ##__VA_ARGS__ \
}

typedef struct {
    xg_sampler_filter_e filter;
} xf_node_copy_pass_params_t;

#define xf_node_copy_pass_params_m( ... ) ( xf_node_copy_pass_params_t ) { \
    .filter = xg_sampler_filter_point_m, \
    ##__VA_ARGS__ \
}

typedef enum {
    xf_texture_clear_color_m,
    xf_texture_clear_depth_stencil_m,
} xf_texture_clear_type_e;

typedef struct {
    xf_texture_clear_type_e type;
    union {
        xg_color_clear_t color;
        xg_depth_stencil_clear_t depth_stencil;
    };
} xf_texture_clear_t;

#define xf_texture_clear_m( ... ) ( xf_texture_clear_t ) { \
    .type = xf_texture_clear_color_m, \
    .color = xg_color_clear_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    // TODO rename to copy_texture_writes?
    xf_texture_clear_t textures[xf_node_max_copy_texture_writes_m];
} xf_node_clear_pass_params_t;

#define xf_node_clear_pass_params_m( ... ) ( xf_node_clear_pass_params_t ) { \
    .textures = { [0 ... xf_node_max_copy_texture_writes_m-1] = xf_texture_clear_m() }, \
    ##__VA_ARGS__ \
}

typedef struct {
    union {
        xf_node_custom_pass_params_t custom;
        xf_node_compute_pass_params_t compute;
        xf_node_copy_pass_params_t copy;
        xf_node_clear_pass_params_t clear;
    };
} xf_node_pass_params_t;

typedef enum {
    xf_node_type_custom_pass_m,
    xf_node_type_compute_pass_m,
    xf_node_type_copy_pass_m,
    xf_node_type_clear_pass_m,
} xf_node_type_e;

typedef struct {
    char debug_name[xf_debug_name_size_m];
    uint32_t debug_color;
    xf_node_type_e type;
    xg_cmd_queue_e queue;
    xf_node_pass_params_t pass;
    xf_node_resource_params_t resources;
    xf_node_passthrough_params_t passthrough;
    xf_node_h node_dependencies[xf_graph_max_nodes_m];
    uint32_t node_dependencies_count;
    xg_resource_bindings_h frame_bindings;
    xg_resource_bindings_h view_bindings;
} xf_node_params_t;

#define xf_node_params_m( ... ) ( xf_node_params_t ) { \
    .debug_name = { 0 }, \
    .debug_color = xg_debug_region_color_none_m, \
    .resources = xf_node_resource_params_m(), \
    .passthrough = xf_node_passthrough_params_m(), \
    .type = xf_node_type_custom_pass_m, \
    .queue = xg_cmd_queue_graphics_m, \
    .pass.custom = xf_node_custom_pass_params_m(), \
    .node_dependencies_count = 0, \
    .frame_bindings = xg_null_handle_m, \
    .view_bindings = xg_null_handle_m, \
    ##__VA_ARGS__ \
}

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
    // To be used only when some usage isn't specified by the graph itself
    // but it is needed anyway for some external interaction (used by a second later graph? TODO test this use case)
    xg_texture_usage_bit_e usage;
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
    .allow_aliasing = true, \
    .debug_name = {0}, \
    .clear_on_create = false, \
    .view_access = xg_texture_view_access_default_only_m, \
    .usage = 0, \
    ##__VA_ARGS__ \
}

typedef struct {
    size_t size;
    bool allow_aliasing;
    bool upload;
    char debug_name[xf_debug_name_size_m];
    bool clear_on_create;
    uint32_t clear_value;
} xf_buffer_params_t;

#define xf_buffer_params_m( ... ) ( xf_buffer_params_t ) { \
    .size = 0, \
    .allow_aliasing = false, \
    .debug_name = {0}, \
    .clear_on_create = false, \
    ##__VA_ARGS__ \
}

typedef struct {
    xf_texture_params_t texture;
    uint32_t multi_texture_count;
    bool auto_advance;
} xf_multi_texture_params_t;

#define xf_multi_texture_params_m( ... ) ( xf_multi_texture_params_t ) { \
    .texture = xf_texture_params_m(), \
    .multi_texture_count = 2, \
    .auto_advance = true, \
    ##__VA_ARGS__ \
}

typedef struct {
    xf_buffer_params_t buffer;
    uint32_t multi_buffer_count;
    bool auto_advance;
} xf_multi_buffer_params_t;

#define xf_multi_buffer_params_m( ... ) ( xf_multi_buffer_params_t ) { \
    .buffer = xf_buffer_params_m(), \
    .multi_buffer_count = 2, \
    .auto_advance = true, \
    ##__VA_ARGS__ \
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
    xg_texture_view_access_e view_access;
    bool allow_aliasing;
    xg_texture_h xg_handle;
    const char* debug_name;
} xf_texture_info_t;

typedef struct {
    uint64_t size;
    bool allow_aliasing;
    const char* debug_name;
} xf_buffer_info_t;

typedef struct {
    xg_device_h device;
    uint32_t node_count;
    xf_node_h nodes[xf_graph_max_nodes_m];
} xf_graph_info_t;

typedef enum {
    xf_resource_access_sampled_m,
    xf_resource_access_uniform_m,
    xf_resource_access_storage_read_m,
    xf_resource_access_copy_read_m,
    xf_resource_access_render_target_m,
    xf_resource_access_depth_target_m,
    xf_resource_access_storage_write_m,
    xf_resource_access_copy_write_m,
    xf_resource_access_invalid_m,
} xf_resource_access_e;

typedef struct {
    xf_texture_h handle;
    xf_resource_access_e access;
} xf_node_texture_info_t;

typedef struct {
    bool enabled;
    bool passthrough;
    xf_node_resource_params_t resources;
    char debug_name[xf_debug_name_size_m];
    xf_node_texture_info_t texture_info[xf_node_max_textures_m];
    uint32_t texture_count;
} xf_node_info_t;

typedef enum {
    xf_graph_flag_sort_m                        = 1 << 0,
    xf_graph_flag_alias_resources_m             = 1 << 1,
    xf_graph_flag_alias_memory_m                = 1 << 2,
    xf_graph_flag_print_node_deps_m             = 1 << 3,
    xf_graph_flag_print_resource_lifespan_m     = 1 << 4,
    xf_graph_flag_print_resource_alias_m        = 1 << 5,
    xf_graph_flag_print_execution_order_m       = 1 << 6,
} xf_graph_flags_e;

typedef struct {
    xg_device_h device;
    xf_graph_flags_e flags;
    char debug_name[xf_debug_name_size_m];
} xf_graph_params_t;

#define xf_graph_params_m( ... ) ( xf_graph_params_t ) { \
    .device = xg_null_handle_m, \
    .flags = xf_graph_flag_alias_resources_m | xf_graph_flag_alias_memory_m, \
    .debug_name = "", \
    ##__VA_ARGS__ \
}

typedef enum {
    xf_export_channel_r,
    xf_export_channel_g,
    xf_export_channel_b,
    xf_export_channel_a,
    xf_export_channel_1,
    xf_export_channel_0,
} xf_export_channel_e;

typedef struct {
    void ( *load_shaders ) ( xg_device_h device );

    xf_texture_h ( *create_texture ) ( const xf_texture_params_t* params );
    xf_buffer_h ( *create_buffer ) ( const xf_buffer_params_t* params );
    xf_texture_h ( *create_multi_texture ) ( const xf_multi_texture_params_t* params );
    xf_buffer_h ( *create_multi_buffer ) ( const xf_multi_buffer_params_t* params );
    void ( *destroy_unreferenced_resources ) ( xg_i* xg, xg_resource_cmd_buffer_h resource_cmd_buffer, xg_resource_cmd_buffer_time_e time );

    void ( *destroy_texture ) ( xf_texture_h texture );

    xf_graph_h ( *create_graph ) ( const xf_graph_params_t* params );
    xf_node_h ( *create_node ) ( xf_graph_h graph, const xf_node_params_t* params ); // TODO rename create_node
    void ( *finalize_graph ) ( xf_graph_h graph );
    uint64_t ( *execute_graph ) ( xf_graph_h graph, xg_workload_h workload, uint64_t base_key );
    void ( *advance_graph_multi_textures ) ( xf_graph_h graph );
    void ( *destroy_graph ) ( xf_graph_h graph, xg_workload_h workload );

    void ( *disable_node ) ( xf_graph_h graph, xf_node_h node );
    void ( *enable_node ) ( xf_graph_h graph, xf_node_h node );
    void ( *node_set_enabled ) ( xf_graph_h graph, xf_node_h node, bool enabled );

    void ( *advance_multi_texture ) ( xf_texture_h multi_texture );
    void ( *advance_multi_buffer ) ( xf_buffer_h multi_buffer );
    xf_texture_h ( *get_multi_texture ) ( xf_texture_h multi_texture, int32_t offset );
    xf_buffer_h ( *get_multi_buffer ) ( xf_texture_h multi_buffer, int32_t offset );

    xf_texture_h ( *create_multi_texture_from_swapchain ) ( xg_swapchain_h swapchain );
    xf_texture_h ( *create_texture_from_external ) ( xg_texture_h texture );
    void ( *refresh_external_texture ) ( xf_texture_h texture );

    void ( *get_texture_info ) ( xf_texture_info_t* info, xf_texture_h texture );
    void ( *get_graph_info ) ( xf_graph_info_t* info, xf_graph_h graph );
    void ( *get_node_info ) ( xf_node_info_t* info, xf_graph_h graph, xf_node_h node );

    void ( *debug_print_graph ) ( xf_graph_h graph );

    void ( *invalidate_graph ) ( xf_graph_h graph, xg_workload_h workload );

    const uint64_t* ( *get_graph_timings ) ( xf_graph_h graph );

    uint32_t ( *list_textures ) ( xf_texture_h* textures, uint32_t capacity );
    void ( *set_graph_texture_export ) ( xf_graph_h graph, xf_node_h node, xf_texture_h texture, xf_texture_h dest, xf_export_channel_e channel_remap[4] );
} xf_i;
