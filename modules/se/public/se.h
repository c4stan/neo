#pragma once

#include <std_module.h>

#define se_module_name_m se
std_module_export_m void* se_load ( void* );
std_module_export_m void se_unload ( void );

typedef uint64_t se_entity_h;
typedef uint64_t se_query_h;

#if 0
    typedef uint32_t se_component_e;
    typedef uint32_t se_component_index_t;
#endif

typedef uint64_t se_component_e;
typedef uint64_t se_component_h;

#define se_null_handle_m UINT64_MAX

typedef uint64_t ( se_component_flags_t ) [se_component_flags_block_count_m];

typedef struct {
    uint32_t max_component_count;
} se_entity_params_t;

#define se_entity_params_m( ... ) ( se_entity_params_t ) { \
    .max_component_count = 8, \
    ##__VA_ARGS__ \
}

typedef struct {
    se_component_flags_t request_component_flags;
    // TODO uint32_t avoid_component_flags ?
} se_query_params_t;

#define se_default_query_params_m ( se_query_params_t ) { 0 }

typedef struct {
    size_t count;
    se_entity_h* entities;
} se_query_result_t;

typedef struct {
    se_entity_h ( *create_entity ) ( const se_entity_params_t* params );
    bool ( *destroy_entity ) ( se_entity_h entity );

    bool ( *add_component ) ( se_entity_h entity, se_component_e component_type, se_component_h component_handle );
    bool ( *remove_component ) ( se_entity_h entity, se_component_e component_type );
    se_component_h ( *get_component ) ( se_entity_h entity, se_component_e component_type );
    void ( *get_components ) ( se_component_h* out_component_handles, size_t count, const se_entity_h* entities, se_component_e component_type );

    se_query_h ( *create_query ) ( const se_query_params_t* params );
    void ( *resolve_pending_queries ) ( void );
    void ( *dispose_query_results ) ( void );
    // TODO return const ptr
    const se_query_result_t* ( *get_query_result ) ( se_query_h query );

#if 0
    // TODO remove individual calls?
    se_entity_h create_entity ( const se_entity_params_t* params );
    bool create_entities ( se_entity_h* entities, const se_entity_params_t* params, size_t count );
    bool destroy_entity ( se_entity_h entity );
    bool destroy_entities ( const se_entity_h* entities, size_t count );

    bool register_component ( se_entity_h entity, se_component_e component_type, se_component_index_t index );
    bool register_components ( const se_entity_h* entities, se_component_e component_type, const se_component_index_t* indices );
    bool unregister_component ( se_entity_h entity, se_component_e component_type );
    bool unregister_components ( const se_entity_h* entities, se_component_e component_type );

    // TODO support multi bases/streams? e.g. for component data laid out in SoA style, to be able to extract directly from SoA streams
    void set_component_data_base ( se_component_e component_type, const void* base );
    void* get_component_data_base ( se_component_e component_type );

    bool get_component_data ( void* data, size_t data_stride, se_component_e component_type, const se_entity_h* entities, size_t count );
    bool get_component_data_streams ( void** streams, const size_t* stream_strides, size_t stream_count, se_component_e component_type, const se_entity_h* entities, size_t entity_count );
    bool get_component_ptrs ( void** pointers, se_component_e component_type, const se_entity_h* entities, size_t count );
    bool get_component_idxs ( se_component_index_t* indices, se_component_e component_type, const se_entity_h* entities, size_t count );

    // TODO is batching queries any good?
    se_query_h create_query ( const se_query_params_t* params );
    se_query_results_h resolve_pending_queries ( void );
    void dispose_query_results ( se_query_results_h query_results );
    bool get_query_result ( se_query_result_t* data, se_query_results_h results, se_query_h query );
#endif
} se_i;
