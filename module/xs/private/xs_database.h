#pragma once

#include <xs.h>
#include <xg.h>

#include <std_time.h>

typedef struct {
    std_alloc_t alloc;
    size_t top;
} xs_database_memory_page_t;

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
} xs_database_pipeline_state_t;

typedef struct {
    const char* path;
} xs_database_pipeline_state_header_t;

typedef struct {
    xs_database_memory_page_t memory_pages[xs_database_max_memory_pages_m];

    const char* folders[xs_database_max_folders_m];
    size_t folders_count;

    xs_database_pipeline_state_t pipeline_states[xs_database_max_pipeline_states_m];
    size_t pipeline_states_count;

    // TODO make these thread safe
    std_hash_map_t pipeline_name_hash_to_state_map; // u64 hash -> xs_database_pipeline_state_t
    //std_hash_map_t pipeline_handle_to_state_map;

    xs_database_pipeline_state_header_t pipeline_state_headers[xs_database_max_pipeline_state_headers_m];
    size_t pipeline_state_headers_count;
    std_timestamp_t pipeline_state_headers_last_build_timestamp;

    char output_path[fs_path_size_m];
} xs_database_state_t;

void xs_database_load ( xs_database_state_t* state );
void xs_database_reload ( xs_database_state_t* state );
void xs_database_unload ( void );

bool xs_database_add_folder ( const char* path );
bool xs_database_set_output_folder ( const char* path );
void xs_database_clear ( void );

xs_database_build_result_t xs_database_build_shaders ( xg_device_h device, const xs_database_build_params_t* params );

xs_pipeline_state_h xs_database_pipeline_lookup ( const char* name );
xs_pipeline_state_h xs_database_pipeline_lookup_hash ( xs_string_hash_t name_hash );
xg_graphics_pipeline_state_h xs_database_pipeline_get ( xs_pipeline_state_h state );
//void xs_database_pipeline_release ( xg_graphics_pipeline_state_h pipeline_handle );

void xs_database_update_pipelines ( xg_workload_h last_workload );
