#include "se_query.h"

#include "se_entity.h"

// TODO use a better allocator
#include <std_allocator.h>
#include <std_mutex.h>
#include <std_log.h>

static se_query_state_t* se_query_state;

#if 0
void se_query_init ( void ) {
    static se_pending_query_t pending_queries_array[se_max_pending_queries_m];
    static se_resolved_query_t resolved_queries_array[se_max_pending_queries_m];

    std_mem_zero_m ( &pending_queries_array );

    se_query_state->pending_queries = pending_queries_array;
    se_query_state->pending_query_count = 0;
    std_mutex_init ( &se_query_state->pending_queries_mutex );

    se_query_state->resolved_queries = resolved_queries_array;
    se_query_state->resolved_query_count = 0;
    std_mutex_init ( &se_query_state->resolved_queries_mutex );
}
#endif

void se_query_load ( se_query_state_t* state ) {
    se_query_state = state;

    state->pending_queries = std_virtual_heap_alloc_array_m ( se_pending_query_t, se_max_pending_queries_m );
    state->pending_query_count = 0;
    std_mutex_init ( &state->pending_queries_mutex );

    state->resolved_queries = std_virtual_heap_alloc_array_m ( se_resolved_query_t, se_max_pending_queries_m );
    state->resolved_query_count = 0;
    std_mutex_init ( &state->resolved_queries_mutex );
}

void se_query_reload ( se_query_state_t* state ) {
    se_query_state = state;
}

void se_query_unload ( void ) {
    std_virtual_heap_free ( se_query_state->pending_queries );
    std_virtual_heap_free ( se_query_state->resolved_queries );
}

#define se_query_handle_idx_bits_m 32
#define se_query_handle_gen_bits_m (64 - se_query_handle_idx_bits_m)

se_query_h se_query_create ( const se_query_params_t* params ) {
    // TODO avoid creating identical queries
    std_mutex_lock ( &se_query_state->pending_queries_mutex );
    se_pending_query_t* query = &se_query_state->pending_queries[se_query_state->pending_query_count++];
    std_mutex_unlock ( &se_query_state->pending_queries_mutex );

    query->params = *params;

    uint64_t query_idx = ( uint64_t ) ( query - se_query_state->pending_queries );
    uint64_t query_gen = query->gen;
    return query_gen << se_query_handle_idx_bits_m | query_idx;
}

void se_query_resolve ( void ) {
    // Extract alive entities
    // TODO better allocation method
    se_component_mask_t* masks = std_virtual_heap_alloc_array_m ( se_component_mask_t, se_max_entities_m );
    se_entity_h* entities = std_virtual_heap_alloc_array_m ( se_entity_h, se_max_entities_m );
    size_t entity_count = se_entity_extract ( masks, entities );

    std_mutex_lock ( &se_query_state->resolved_queries_mutex );

    if ( se_query_state->resolved_query_count != 0 ) {
        std_log_error_m ( "Need to dispose of previously resolved queries before triggering another resolve" );
        std_mutex_unlock ( &se_query_state->resolved_queries_mutex );
        std_virtual_heap_free ( masks );
        std_virtual_heap_free ( entities );
        return;
    }

    std_mutex_lock ( &se_query_state->pending_queries_mutex );

    for ( size_t query_it = 0; query_it < se_query_state->pending_query_count; ++query_it ) {
        se_pending_query_t* pending_query = &se_query_state->pending_queries[query_it];
        se_resolved_query_t* resolved_query = &se_query_state->resolved_queries[query_it];

        // TODO reserve virtual memory and only commit as many chunks as needed
        se_entity_h* result_entities = std_virtual_heap_alloc_array_m ( se_entity_h, entity_count );

        // TODO vectorize
        //se_entity_h* result_entities = ( se_entity_h* ) resolved_query->result_alloc.buffer.base;
        size_t result_entity_count = 0;

        for ( size_t entity_it = 0; entity_it < entity_count; ++entity_it ) {
            bool match = true;

            for ( size_t block_it = 0; block_it < se_component_mask_block_count_m; ++block_it ) {
                uint64_t entity_block = masks[entity_it].u64[block_it];
                uint64_t request_block = pending_query->params.request_component_mask.u64[block_it];
                match &= ( request_block & entity_block ) == request_block;
            }

            if ( match ) {
                result_entities[result_entity_count++] = entities[entity_it];
            }
        }

        resolved_query->result.entities = result_entities;
        resolved_query->result.count = result_entity_count;
        resolved_query->gen = pending_query->gen++;
    }

    size_t resolved_query_count = se_query_state->pending_query_count;
    se_query_state->pending_query_count = 0;

    std_mutex_unlock ( &se_query_state->pending_queries_mutex );

    se_query_state->resolved_query_count = resolved_query_count;

    std_mutex_unlock ( &se_query_state->resolved_queries_mutex );

    std_virtual_heap_free ( masks );
    std_virtual_heap_free ( entities );
}

void se_query_results_dispose ( void ) {
    std_mutex_lock ( &se_query_state->resolved_queries_mutex );

    size_t count = se_query_state->resolved_query_count;

    // TODO avoid freeing and reallocating this every time?
    for ( size_t i = 0; i < count; ++i ) {
        se_resolved_query_t* query = &se_query_state->resolved_queries[i];
        std_virtual_heap_free ( query->result.entities );
        ++query->gen;
    }

    se_query_state->resolved_query_count = 0;

    std_mutex_unlock ( &se_query_state->resolved_queries_mutex );
}

const se_query_result_t* se_query_results_get ( se_query_h query_handle ) {
    std_mutex_lock ( &se_query_state->resolved_queries_mutex );

    if ( se_query_state->resolved_query_count == 0 ) {
        std_log_error_m ( "Trying to access stale or invalid query result" );
        std_mutex_unlock ( &se_query_state->resolved_queries_mutex );
        return NULL;
    }

    uint64_t gen = std_bit_read_ms_64 ( query_handle, se_query_handle_gen_bits_m );
    uint64_t idx = std_bit_read_ls_64 ( query_handle, se_query_handle_idx_bits_m );

    se_resolved_query_t* query = &se_query_state->resolved_queries[idx];

    if ( query->gen != gen ) {
        std_log_error_m ( "Trying to access stale or invalid query result" );
        std_mutex_unlock ( &se_query_state->resolved_queries_mutex );
        return NULL;
    }

    se_query_result_t* result = &query->result;

    std_mutex_unlock ( &se_query_state->resolved_queries_mutex );

    return result;
}
