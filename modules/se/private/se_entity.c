#include "se_entity.h"

#include <std_mutex.h>
#include <std_list.h>
#include <std_hash.h>
#include <std_log.h>

typedef struct {
    std_mutex_t mutex;

    uint64_t* entity_bitset;

    se_component_flags_t* component_flags_array;
    se_component_flags_t* component_flags_freelist;

    se_entity_component_table_t* component_tables;
    // component_tables_heap;
} se_entity_state_t;

static se_entity_state_t se_entity_state;

void se_entity_init ( void ) {
    static uint64_t entity_bitset_array[se_max_entities_m / 64];
    std_assert_m ( std_align_test ( se_max_entities_m, 64 ) );

    static se_component_flags_t component_flags_array[se_max_entities_m];
    static se_entity_component_table_t component_tables_array[se_max_entities_m];

    se_entity_state.component_flags_array = component_flags_array;
    se_entity_state.component_flags_freelist = std_static_freelist_m ( component_flags_array );
    se_entity_state.component_tables = component_tables_array;

    std_mem_zero_m ( &entity_bitset_array );
    se_entity_state.entity_bitset = entity_bitset_array;

    std_mutex_init ( &se_entity_state.mutex );
}

se_entity_h se_entity_create ( const se_entity_params_t* params ) {
    // TODO
    std_unused_m ( params );

    std_mutex_lock ( &se_entity_state.mutex );
    se_component_flags_t* component_flags = std_list_pop_m ( &se_entity_state.component_flags_freelist );
    size_t entity_idx = ( size_t ) ( component_flags - se_entity_state.component_flags_array );
    std_bitset_set ( se_entity_state.entity_bitset, entity_idx );
    std_mutex_unlock ( &se_entity_state.mutex );

    se_entity_component_table_t* component_table = &se_entity_state.component_tables[entity_idx];
    std_mem_zero_m ( component_flags );
    std_mem_zero_m ( component_table );
    std_mem_set ( component_table->keys, sizeof ( component_table->keys ), std_byte_max_m );
    component_table->mask = 32 - 1;

    return ( se_entity_h ) entity_idx;
}

bool se_entity_destroy ( se_entity_h entity_handle ) {
    uint64_t entity_idx = ( uint64_t ) entity_handle;

    std_mutex_lock ( &se_entity_state.mutex );
    se_component_flags_t* component_flags = &se_entity_state.component_flags_array[entity_idx];
    std_list_push ( &se_entity_state.component_flags_freelist, component_flags );
    std_bitset_clear ( se_entity_state.entity_bitset, entity_idx );
    std_mutex_unlock ( &se_entity_state.mutex );

    // TODO
    return true;
}

bool se_entity_component_add ( se_entity_h entity_handle, se_component_e component_type, se_component_h component_handle ) {
    uint64_t entity_idx = ( uint64_t ) entity_handle;
    se_entity_component_table_t* component_table = &se_entity_state.component_tables[entity_idx];

    std_assert_m ( std_bitset_test ( se_entity_state.entity_bitset, entity_idx ) );

    // TODO
    uint64_t hash = std_hash_murmur_mixer_64 ( component_type );
    //std_assert_m ( hash != 0 );

    std_hash_map_t map;
    map.hashes = component_table->keys;
    map.payloads = component_table->payloads;
    map.count = component_table->count;
    map.mask = component_table->mask;

    bool result = std_hash_map_insert ( &map, hash, component_handle );

    if ( result ) {
        component_table->count = map.count;
        se_component_flags_t* component_flags = &se_entity_state.component_flags_array[entity_idx];
        std_bitset_set ( component_flags, component_type );
    }

    return result;
}

bool se_entity_component_remove ( se_entity_h entity_handle, se_component_e component_type ) {
    uint64_t entity_idx = ( uint64_t ) entity_handle;
    se_entity_component_table_t* component_table = &se_entity_state.component_tables[entity_idx];

    std_assert_m ( std_bitset_test ( se_entity_state.entity_bitset, entity_idx ) );

    // TODO
    uint64_t hash = std_hash_murmur_mixer_64 ( component_type );
    //std_assert_m ( hash != 0 );

    std_hash_map_t map;
    map.hashes = component_table->keys;
    map.payloads = component_table->payloads;
    map.count = component_table->count;
    map.mask = component_table->mask;

    bool result = std_hash_map_remove ( &map, hash );

    if ( result ) {
        component_table->count = map.count;
        se_component_flags_t* component_flags = &se_entity_state.component_flags_array[entity_idx];
        std_bitset_clear ( component_flags, component_type );
    }

    return result;
}

se_component_h se_entity_component_get ( se_entity_h entity_handle, se_component_e component_type ) {
    uint64_t entity_idx = ( uint64_t ) entity_handle;
    se_entity_component_table_t* component_table = &se_entity_state.component_tables[entity_idx];

    std_assert_m ( std_bitset_test ( se_entity_state.entity_bitset, entity_idx ) );

    // TODO
    uint64_t hash = std_hash_murmur_mixer_64 ( component_type );
    //std_assert_m ( hash != 0 );

    std_hash_map_t map;
    map.hashes = component_table->keys;
    map.payloads = component_table->payloads;
    map.count = component_table->count;
    map.mask = component_table->mask;

    uint64_t* result = std_hash_map_lookup ( &map, hash );

    if ( result == NULL ) {
        return se_null_handle_m;
    }

    return ( se_component_h ) ( *result );
}

void se_entity_component_get_array ( se_component_h* out_component_handles, size_t count, const se_entity_h* entities, se_component_e component_type ) {
    for ( size_t i = 0; i < count; ++i ) {
        out_component_handles[i] = se_entity_component_get ( entities[i], component_type );
    }
}

size_t se_entity_extract ( se_component_flags_t* out_component_flags, se_entity_h* out_entity_handles ) {
    size_t block_count = se_max_entities_m / 64;
    uint64_t entity_idx = 0;
    size_t count = 0;

    std_mutex_lock ( &se_entity_state.mutex );

    while ( std_bitset_scan ( &entity_idx, se_entity_state.entity_bitset, entity_idx, block_count ) ) {
        se_component_flags_t* component_flags = &se_entity_state.component_flags_array[entity_idx];
        se_entity_h entity_handle = ( se_entity_h ) entity_idx;

        std_mem_copy ( out_component_flags[count], *component_flags, sizeof ( se_component_flags_t ) );
        //out_component_flags[count] = *component_flags;
        out_entity_handles[count] = entity_handle;

        ++count;
        ++entity_idx;
    }

    std_mutex_unlock ( &se_entity_state.mutex );

    return count;
}
