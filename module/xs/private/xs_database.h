#pragma once

#include <xs.h>
#include <xg.h>

#include <std_time.h>

//typedef struct {
//    std_alloc_t alloc;
//    size_t top;
//} xs_database_memory_page_t;

typedef struct {
    xs_database_build_params_t params;
    xs_database_build_result_t result;
} xs_database_build_t;

typedef struct {
    // These both point to xs_database_memory_page_t allocated memory
    const char* path;
    const char* name;
    uint64_t name_hash;
    xg_pipeline_e type;
    xg_pipeline_state_h pipeline_handle;
    xg_pipeline_state_h old_pipeline_handle;
    xg_workload_h old_pipeline_workload;
    std_timestamp_t last_build_timestamp;
    //uint32_t permutation_id;
    //uint32_t reference_count;
    xg_resource_bindings_layout_h resource_layouts[xg_shader_binding_set_count_m];
} xs_database_pipeline_state_t;

typedef struct {
    const char* path;
} xs_database_pipeline_state_header_t;

typedef struct {
    xg_device_h device;
    std_virtual_stack_t stack;

    // Build params
    bool dirty_build_params;
    xg_graphics_pipeline_state_t base_graphics_state;
    xg_compute_pipeline_state_t base_compute_state;
    xg_raytrace_pipeline_state_t base_raytrace_state;
    xs_shader_definition_t global_definitions[xs_database_build_max_global_definitions_m];
    uint32_t global_definition_count;

    const char* folders[xs_database_max_folders_m];
    size_t folders_count;

    xs_database_pipeline_state_t pipeline_states[xs_database_max_pipeline_states_m];
    size_t pipeline_states_count;

    std_hash_map_t pipeline_name_hash_to_state_map; // u64 hash -> xs_database_pipeline_state_t

    xs_database_pipeline_state_header_t* pipeline_state_headers_array;//[];
    size_t pipeline_state_headers_count;
    std_timestamp_t pipeline_state_headers_last_build_timestamp;

    char output_path[std_path_size_m];
    char debug_name[32];
} xs_database_t;

#define xs_database_bitset_u64_count_m std_div_ceil_m ( xs_database_max_databases_m, 64 )

typedef struct {
    xs_database_t* database_array;
    xs_database_t* database_freelist;
    uint64_t database_bitset[xs_database_bitset_u64_count_m];
} xs_database_state_t;

void xs_database_load ( xs_database_state_t* state );
void xs_database_reload ( xs_database_state_t* state );
void xs_database_unload ( void );

xs_database_h xs_database_create ( const xs_database_params_t* params );
void xs_database_destroy ( xs_database_h database );

bool xs_database_add_folder ( xs_database_h database, const char* path );
bool xs_database_set_output_folder ( xs_database_h database, const char* path );
void xs_database_clear ( xs_database_h database );

void xs_database_set_build_params ( xs_database_h database, const xs_database_build_params_t* params );
xs_database_build_result_t xs_database_build ( xs_database_h database );
void xs_database_rebuild_all ( void );
xs_database_pipeline_h xs_database_pipeline_get ( xs_database_h database, xs_string_hash_t name_hash );
xg_graphics_pipeline_state_h xs_database_pipeline_state_get ( xs_database_pipeline_h state );
void xs_database_update_pipelines ( xg_workload_h last_workload );

