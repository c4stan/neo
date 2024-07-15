#include "se_entity.h"

#include <std_mutex.h>
#include <std_list.h>
#include <std_hash.h>
#include <std_log.h>
#include <std_atomic.h>

static se_entity_state_t* se_entity_state;

void se_entity_load ( se_entity_state_t* state ) {
    se_entity_state = state;

    std_mem_zero_m ( &state->family_bitset );
    state->family_mask_array = std_virtual_heap_alloc_array_m ( se_component_mask_t, se_entity_max_families_m );
    state->family_array = std_virtual_heap_alloc_array_m ( se_entity_family_t, se_entity_max_families_m );
    state->family_freelist = std_freelist_m ( state->family_array, se_entity_max_families_m );
    state->family_map_hashes = std_virtual_heap_alloc_array_m ( uint64_t, se_entity_max_families_m * 2 );
    state->family_map_values = std_virtual_heap_alloc_array_m ( uint64_t, se_entity_max_families_m * 2 );
    state->family_map = std_hash_map ( state->family_map_hashes, state->family_map_values, se_entity_max_families_m * 2 );

    state->page_array = std_virtual_heap_alloc_array_m ( void*, se_entity_family_page_count_m );
    for ( uint64_t i = 0; i < se_entity_family_page_count_m; ++i ) {
        state->page_array[i] = std_virtual_heap_alloc ( se_entity_family_page_size_m, 16 );
    }
    state->page_count = se_entity_family_page_count_m;

    state->entity_array = std_virtual_heap_alloc_array_m ( se_entity_t, se_max_entities_m );
    state->entity_freelist = std_freelist_m ( state->entity_array, se_max_entities_m );

    state->entity_destroy_array = std_virtual_heap_alloc_array_m ( se_entity_h, se_max_entities_m );
    state->entity_destroy_count = 0;

    std_mutex_init ( &se_entity_state->mutex );

    state->entity_meta = std_virtual_heap_alloc_m ( se_entity_metadata_t );
    state->component_meta = std_virtual_heap_alloc_m ( se_entity_component_metadata_t );
    std_mem_zero_m ( state->entity_meta );
    std_mem_zero_m ( state->component_meta );
}

void se_entity_reload ( se_entity_state_t* state ) {
    se_entity_state = state;
}

void se_entity_unload ( void ) {
    // TODO
}

#if 0
void se_entity_init ( void ) {
    static uint64_t entity_bitset_array[se_max_entities_m / 64];
    std_assert_m ( std_align_test ( se_max_entities_m, 64 ) );

    static se_component_flags_t component_flags_array[se_max_entities_m];
    static se_entity_component_table_t component_tables_array[se_max_entities_m];

    se_entity_state->component_flags_array = component_flags_array;
    se_entity_state->component_flags_freelist = std_static_freelist_m ( component_flags_array );
    se_entity_state->component_tables = component_tables_array;

    std_mem_zero_m ( &entity_bitset_array );
    se_entity_state->entity_bitset = entity_bitset_array;

    std_mutex_init ( &se_entity_state->mutex );
}
#endif

#if 0
se_entity_h se_entity_create ( const se_entity_params_t* params ) {
    // TODO
    std_unused_m ( params );

    std_mutex_lock ( &se_entity_state->mutex );
    se_component_mask_t* mask = std_list_pop_m ( &se_entity_state->component_mask_freelist );
    size_t entity_idx = ( size_t ) ( mask - se_entity_state->component_mask_array );
    std_bitset_set ( se_entity_state->entity_bitset, entity_idx );
    std_mutex_unlock ( &se_entity_state->mutex );

    se_entity_component_table_t* component_table = &se_entity_state->component_tables[entity_idx];
    std_mem_zero_m ( &mask );
    std_mem_zero_m ( component_table );
    std_mem_set ( component_table->keys, sizeof ( component_table->keys ), 0xff );
    component_table->mask = 32 - 1;

    return ( se_entity_h ) entity_idx;
}

bool se_entity_destroy ( se_entity_h entity_handle ) {
    uint64_t entity_idx = ( uint64_t ) entity_handle;

    std_mutex_lock ( &se_entity_state->mutex );
    se_component_mask_t* mask = &se_entity_state->component_mask_array[entity_idx];
    std_list_push ( &se_entity_state->component_mask_freelist, &mask );
    std_bitset_clear ( se_entity_state->entity_bitset, entity_idx );
    std_mutex_unlock ( &se_entity_state->mutex );

    // TODO
    return true;
}

bool se_entity_component_add ( se_entity_h entity_handle, se_component_e component_type, se_component_h component_handle ) {
    uint64_t entity_idx = ( uint64_t ) entity_handle;
    se_entity_component_table_t* component_table = &se_entity_state->component_tables[entity_idx];

    std_assert_m ( std_bitset_test ( se_entity_state->entity_bitset, entity_idx ) );

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
        se_component_mask_t* mask = &se_entity_state->component_mask_array[entity_idx];
        std_bitset_set ( mask->u64, component_type );
    }

    return result;
}

bool se_entity_component_remove ( se_entity_h entity_handle, se_component_e component_type ) {
    uint64_t entity_idx = ( uint64_t ) entity_handle;
    se_entity_component_table_t* component_table = &se_entity_state->component_tables[entity_idx];

    std_assert_m ( std_bitset_test ( se_entity_state->entity_bitset, entity_idx ) );

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
        se_component_mask_t* mask = &se_entity_state->component_mask_array[entity_idx];
        std_bitset_clear ( mask->u64, component_type );
    }

    return result;
}

se_component_h se_entity_component_get ( se_entity_h entity_handle, se_component_e component_type ) {
    uint64_t entity_idx = ( uint64_t ) entity_handle;
    se_entity_component_table_t* component_table = &se_entity_state->component_tables[entity_idx];

    std_assert_m ( std_bitset_test ( se_entity_state->entity_bitset, entity_idx ) );

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

size_t se_entity_extract ( se_component_mask_t* out_component_masks, se_entity_h* out_entity_handles ) {
    size_t block_count = se_max_entities_m / 64;
    uint64_t entity_idx = 0;
    size_t count = 0;

    std_mutex_lock ( &se_entity_state->mutex );

    while ( std_bitset_scan ( &entity_idx, se_entity_state->entity_bitset, entity_idx, block_count ) ) {
        se_component_mask_t* component_mask = &se_entity_state->component_mask_array[entity_idx];
        se_entity_h entity_handle = ( se_entity_h ) entity_idx;

        //std_mem_copy ( out_component_flags[count], *component_flags, sizeof ( se_component_flags_t ) );
        out_component_masks[count] = *component_mask;
        //out_component_flags[count] = *component_flags;
        out_entity_handles[count] = entity_handle;

        ++count;
        ++entity_idx;
    }

    std_mutex_unlock ( &se_entity_state->mutex );

    return count;
}
#endif

//--


static uint64_t se_entity_component_mask_hash ( se_component_mask_t mask ) {
    std_hash_metro_state_t hash_state;
    std_hash_metro_begin ( &hash_state );

    for ( uint32_t i = 0; i < se_component_mask_block_count_m; ++i ) {
        std_hash_metro_add_m ( &hash_state, &mask.u64[i] );
    }

    uint64_t hash = std_hash_metro_end ( &hash_state );
    return hash;
}

void se_entity_family_create ( const se_entity_family_params_t* params ) {
    uint32_t component_count = params->component_count;

    se_entity_family_t* family = std_list_pop_m ( &se_entity_state->family_freelist );

    family->component_count = component_count;
    family->entity_count = 0;
    std_mem_set ( family->component_slots, sizeof ( family->component_slots ), 0xff );

    se_component_mask_t mask;
    std_mem_zero_m ( mask.u64 );

    for ( uint32_t i = 0; i < component_count; ++i ) {
        const se_component_layout_t* component = &params->components[i];
        uint32_t id = component->id;

        std_bitset_set ( mask.u64, id );
        family->component_slots[id] = i;

        family->components[i].id = id;
        family->components[i].stream_count = component->stream_count;

        for ( uint32_t j = 0; j < component->stream_count; ++j ) {
            //uint32_t stride = component->streams[j].stride;
            uint32_t stride = component->streams[j];

            family->components[i].streams[j].stride = stride;
            family->components[i].streams[j].items_per_page = se_entity_family_page_size_m / stride;
            family->components[i].streams[j].page_count = 1;
            family->components[i].streams[j].pages[0] = se_entity_state->page_array[--se_entity_state->page_count];
        }
    }

    family->entity_stream.stride = sizeof ( se_entity_h );
    family->entity_stream.items_per_page = se_entity_family_page_size_m / sizeof ( se_entity_h );
    family->entity_stream.page_count = 1;
    family->entity_stream.pages[0] = se_entity_state->page_array[--se_entity_state->page_count];

    uint64_t family_idx = family - se_entity_state->family_array;
    se_entity_state->family_mask_array[family_idx] = mask;

    uint64_t hash = se_entity_component_mask_hash ( mask );
    std_hash_map_insert ( &se_entity_state->family_map, hash, family_idx );

    std_bitset_set ( se_entity_state->family_bitset, family_idx );
}

static se_entity_family_t* se_entity_family_get ( se_component_mask_t mask ) {
    uint64_t hash = se_entity_component_mask_hash ( mask );
    uint64_t* idx = std_hash_map_lookup ( &se_entity_state->family_map, hash );
    
    if ( !idx ) {
        return NULL;
    }

    se_entity_family_t* family = &se_entity_state->family_array[*idx];

    return family;
}

void se_entity_family_destroy ( se_component_mask_t mask ) {
    se_entity_family_t* family = se_entity_family_get ( mask );

    if ( !family ) {
        std_log_error_m ( "Trying to delete non-existing entity family" );
        return;
    }

    for ( uint32_t i = 0; i < family->component_count; ++i ) {
        se_entity_family_component_t* component = &family->components[i];
        
        for ( uint32_t j = 0; j < component->stream_count; ++j ) {
            se_entity_family_stream_t* stream = &component->streams[j];

            for ( uint32_t k = 0; k < stream->page_count; ++k ) {
                se_entity_state->page_array[se_entity_state->page_count++] = stream->pages[k];
            }
        }
    }

    {
        se_entity_family_stream_t* stream = &family->entity_stream;

        for ( uint32_t k = 0; k < stream->page_count; ++k ) {
            se_entity_state->page_array[se_entity_state->page_count++] = stream->pages[k];
        }
    }

    std_list_push ( &se_entity_state->family_freelist, family );

    uint64_t family_idx = family - se_entity_state->family_array;
    std_bitset_clear ( se_entity_state->family_bitset, family_idx );
}

#if 0
static se_entity_h* se_entity_family_get_entity ( se_entity_family_t* family, uint32_t idx ) {
    se_entity_family_stream_t* stream = &family->entity_stream;
    
    uint32_t entities_per_page = stream->stride / se_entity_family_page_size_m;
    uint32_t page_idx = idx / entities_per_page;
    uint32_t page_sub_idx = idx % entities_per_page;

    std_assert_m ( page_idx < stream->page_count );

    se_entity_h* page = stream->pages[page_idx];
    return &page[page_sub_idx];
}
#endif

static void* se_entity_family_get_component ( se_entity_family_t* family, uint32_t entity_idx, uint32_t component_id, uint8_t stream_id ) {
    std_assert_m ( component_id < se_max_component_types_m );
    uint8_t component_family_slot = family->component_slots[component_id];
    std_assert_m ( component_family_slot < se_entity_max_components_per_entity_m );
    se_entity_family_component_t* family_component = &family->components[component_family_slot];
    se_entity_family_stream_t* family_stream = &family_component->streams[stream_id];

    uint32_t stride = family_stream->stride;
    uint32_t items_per_page = family_stream->items_per_page;
    uint32_t page_idx = entity_idx / items_per_page;
    uint32_t page_sub_idx = entity_idx % items_per_page;

    void* data = family_stream->pages[page_idx] + page_sub_idx * stride;
    return data;
}

static void* se_entity_family_get_component_stream_data ( se_entity_t* entity, se_entity_family_stream_t* stream ) {
    uint32_t idx = entity->idx;

    uint32_t stride = stream->stride;
    uint32_t items_per_page = stream->items_per_page;
    uint32_t page_idx = idx / items_per_page;
    uint32_t page_sub_idx = idx % items_per_page;   

    return stream->pages[page_idx] + page_sub_idx * stride;
}

static const void* se_entity_update ( const se_entity_update_t* update ) {
    //for ( uint64_t update_it = 0; update_it < update_count; ++update_it ) {
    //se_entity_update_t* update = &updates[update_it];
    se_entity_t* entity = &se_entity_state->entity_array[update->entity];
    //se_entity_family_t* family = se_entity_family_get ( entity->mask );
    se_entity_family_t* family = &se_entity_state->family_array[entity->family];

    const se_component_update_t* component = update->components;
    const void* ptr = component;

    for ( uint32_t i = 0; i < update->component_count; ++i ) {
        //const se_component_update_t* component = &update->components[i];
        
        uint32_t id = component->id;
        std_assert_m ( id < se_max_component_types_m );

        uint8_t family_slot = family->component_slots[id];
        std_assert_m ( family_slot < se_entity_max_components_per_entity_m );
        se_entity_family_component_t* family_component = &family->components[family_slot];

        ptr += sizeof ( se_component_update_t );

        for ( uint32_t j = 0; j < component->stream_count; ++j ) {
            const se_component_stream_update_t* stream = &component->streams[j];
            bool has_inline_data = stream->has_inline_data;
            void* src_data_ptr = stream->data;

            se_entity_family_stream_t* family_stream = &family_component->streams[stream->id];
            void* dst_data = se_entity_family_get_component_stream_data ( entity, family_stream );
            uint32_t stride = family_stream->stride;

            if ( !has_inline_data ) {
                if ( src_data_ptr ) {
                    std_mem_copy ( dst_data, src_data_ptr, stride );
                }
                ptr += sizeof ( se_component_stream_update_t );
            } else {
                std_mem_copy ( dst_data, stream->inline_data, stride );
                ptr += 8 + stride;
            }
        }

        //ptr += sizeof ( se_component_update_t ) + component->stream_count * sizeof ( se_component_stream_update_t );
        component = ( const se_component_update_t* ) ptr;
    }

    return ptr;
    //}
}

void se_entity_alloc_components ( se_entity_h entity_handle, se_component_mask_t mask ) {
    se_entity_t* entity = &se_entity_state->entity_array[entity_handle];
    se_entity_family_t* family = se_entity_family_get ( mask );
    std_assert_m ( family );

    //entity->mask = mask;
    entity->family = family - se_entity_state->family_array;

    // allocate component pages
    uint32_t family_idx = family->entity_count++;

    entity->idx = family_idx;

    for ( uint32_t i = 0; i < family->component_count; ++i ) {
        se_entity_family_component_t* component = &family->components[i];

        for ( uint32_t j = 0; j < component->stream_count; ++j ) {
            se_entity_family_stream_t* stream = &component->streams[j];

            uint32_t items_per_page = stream->items_per_page;
            uint32_t page_idx = family_idx / items_per_page;

            while ( stream->page_count < page_idx + 1 ) {
                stream->pages[stream->page_count++] = se_entity_state->page_array[--se_entity_state->page_count];
            }

            // zero init
            uint32_t page_sub_idx = family_idx % items_per_page;
            uint32_t stride = stream->stride;
            std_mem_zero ( stream->pages[page_idx] + stride * page_sub_idx, stride );
        }
    }
}

se_entity_h se_entity_reserve ( void ) {
    se_entity_t* entity = std_list_pop_m ( &se_entity_state->entity_freelist );

    if ( !entity ) {
        return se_null_handle_m;
    }

    uint64_t entity_idx = entity - se_entity_state->entity_array;
    
    std_bitset_set ( se_entity_state->entity_meta->used_entities, entity_idx );

    return  ( se_entity_h ) entity_idx;
}

se_entity_h se_entity_alloc ( se_component_mask_t mask ) {
    // make sure entity faimly exists
    //se_entity_family_t* family = se_entity_family_get ( mask );
    se_entity_family_t* family = se_entity_family_get ( mask );
    if ( !family ) {
        std_log_error_m ( "Trying to add entity to non-existing entity family" );
        return se_null_handle_m;
    }

    // allocate entity
#if 0
    se_entity_t* entity = std_list_pop_m ( &se_entity_state->entity_freelist );
    uint64_t entity_idx = entity - se_entity_state->entity_array;
    se_entity_h entity_handle = entity_idx;
#else
    se_entity_h entity_handle = se_entity_reserve();
#endif
    se_entity_alloc_components ( entity_handle, mask );

    return entity_handle;
}

const char* se_entity_name ( se_entity_h entity_handle ) {
    se_entity_name_t* name = &se_entity_state->entity_meta->names[entity_handle];
    return name->string;
}

void se_entity_create ( const se_entity_params_t* params ) {
    uint64_t count = params->entity_count;
    const se_entity_update_t* entity_update = params->entities;

    for ( uint64_t entity_it = 0; entity_it < count; ++entity_it ) {
        //se_entity_h entity_handle = se_entity_alloc ( entity_update->mask );
        se_entity_alloc_components ( entity_update->entity, entity_update->mask );
        entity_update = ( se_entity_update_t* ) se_entity_update ( entity_update );
    }
}

void se_entity_destroy ( const se_entity_h* entity_handles, uint64_t count ) {
    std_not_implemented_m();
    std_unused_m ( entity_handles );
    std_unused_m ( count );
}

void se_entity_query ( se_query_result_t* result, const se_query_params_t* params ) {
#if 1
    // fill query mask from query components
    se_component_mask_t query_mask;
    std_mem_zero_m ( &query_mask );

    for ( uint32_t i = 0; i < params->component_count; ++i ) {
        std_bitset_set ( query_mask.u64, params->components[i] );
    }
#else
    se_component_mask_t query_mask = params->mask;
#endif

    // clear result
    std_mem_zero_m ( result );

    // iterate entity families
    uint64_t family_idx = 0;
    while ( std_bitset_scan ( &family_idx, se_entity_state->family_bitset, family_idx, std_entity_family_bitset_block_count_m ) ) {
        se_component_mask_t mask = se_entity_state->family_mask_array[family_idx];
        bool skip = false;

        for ( uint32_t i = 0; i < se_component_mask_block_count_m; ++i ) {
            if ( ( query_mask.u64[i] & mask.u64[i] ) != query_mask.u64[i] ) {
                skip = true;
                break;
            }
        }

        if ( skip ) {
            ++family_idx;
            continue;
        }

        se_entity_family_t* family = &se_entity_state->family_array[family_idx];
        uint32_t entity_count = family->entity_count;

#if 1
        for ( uint32_t i = 0; i < params->component_count; ++i ) {
            uint32_t component_id = params->components[i];
#else
        uint64_t component_idx = 0;
        uint32_t i = 0;
        while ( std_bitset_scan ( &component_idx, query_mask.u64, component_idx, se_component_mask_block_count_m ) ) {
            uint32_t component_id = component_idx;
#endif
            uint8_t family_slot = family->component_slots[component_id];
            std_assert_m ( family_slot != 0xff );
            se_entity_family_component_t* component = &family->components[family_slot];

            se_component_t* result_component = &result->components[i];
            
            result_component->stream_count = component->stream_count;

            for ( uint32_t j = 0; j < component->stream_count; ++j ) {
                const se_entity_family_stream_t* stream = &component->streams[j];
                
                uint32_t capacity = stream->items_per_page;
                
                result_component->streams[j].page_capacity = capacity;
                result_component->streams[j].data_stride = stream->stride;

                uint32_t base = result_component->streams[j].page_count;
                uint32_t count = 0;

                for ( uint32_t k = 0; k < stream->page_count; ++k ) {
                    result_component->streams[j].pages[base + k].data = stream->pages[k];
                    result_component->streams[j].pages[base + k].count += count + capacity <= entity_count ? capacity : entity_count - count;
                    
                    count += capacity;
                }

                result_component->streams[j].page_count += stream->page_count;
                std_assert_m ( result_component->streams[j].page_count <= se_entity_family_max_pages_per_stream_m );
            }
        }

        result->entity_count += entity_count;
    
        ++family_idx;
    }
}

void* se_entity_get_component ( se_entity_h entity_handle, se_component_e component, uint8_t stream ) {
    se_entity_t* entity = &se_entity_state->entity_array[entity_handle];

    uint32_t family_idx = entity->family;
    uint32_t family_entity_idx = entity->idx;

    se_entity_family_t* family = &se_entity_state->family_array[family_idx];
    return se_entity_family_get_component ( family, family_entity_idx, component, stream );
}

//

void se_entity_set_name ( se_entity_h entity_handle, const char* name ) {
    se_entity_name_t* entity_name = &se_entity_state->entity_meta->names[entity_handle];
    std_str_copy_static_m ( entity_name->string, name );
}

void se_entity_set_component_properties ( se_component_e component_id, const char* name, const se_component_properties_params_t* params ) {
    se_entity_component_properties_t* component = &se_entity_state->component_meta->property_array[component_id];

    std_str_copy_static_m ( component->name, name );

    std_assert_m ( component->property_count + params->count < se_component_max_properties_m );

    for ( uint32_t i = 0; i < params->count; ++i ) {
        uint32_t idx = component->property_count + i;
        
        if ( idx >= se_component_max_properties_m ) {
            break;
        }

        component->properties[idx] = params->properties[i];
    }
    
    component->property_count += params->count;
}

size_t se_entity_list ( se_entity_h* out_entities, size_t cap ) {
    size_t block_count = std_static_array_capacity_m ( se_entity_state->entity_meta->used_entities );
    uint64_t entity_idx = 0;
    size_t count = 0;

    if ( cap < se_max_entities_m ) {
        std_log_warn_m ( "Provided buffer might not be able to contain all entities, use se_max_entities_m as capacity." );
    }

    while ( std_bitset_scan ( &entity_idx, se_entity_state->entity_meta->used_entities, entity_idx, block_count ) ) {
        se_entity_h entity_handle = ( se_entity_h ) entity_idx;

        if ( count >= cap ) { 
            std_log_error_m ( "Provided buffer not big enough, failed to list all entities." );
            return count;
        }

        out_entities[count] = entity_handle;

        ++count;
        ++entity_idx;
    }

    return count;
}

void se_entity_property_get ( se_entity_h entity_handle, se_entity_properties_t* out_props ) {
    uint64_t entity_idx = entity_handle;
    std_assert_m ( std_bitset_test ( se_entity_state->entity_meta->used_entities, entity_idx ) );

    std_str_copy_static_m ( out_props->name, se_entity_state->entity_meta->names[entity_idx].string );

    se_entity_t* entity = &se_entity_state->entity_array[entity_idx];
    se_entity_family_t* family = &se_entity_state->family_array[entity->family];

    out_props->component_count = family->component_count;

    for ( uint32_t i = 0; i < family->component_count; ++i ) {
        se_entity_family_component_t* family_component = &family->components[i];
        se_component_e component_id = family_component->id;

        se_entity_component_properties_t* component_properties = &se_entity_state->component_meta->property_array[component_id];
        se_component_properties_t* out_component_properties = &out_props->components[i];
        
        out_component_properties->id = component_id;
        out_component_properties->property_count = component_properties->property_count;
        std_str_copy_static_m ( out_component_properties->name, component_properties->name );

        for ( uint32_t j = 0; j < component_properties->property_count; ++j ) {
            out_component_properties->properties[j] = component_properties->properties[j];
            out_component_properties->handles[j] = ( se_property_h ) { entity_handle, component_id,  j };
        }
    }
}

static uint32_t se_entity_property_stride ( se_property_e property ) {
    switch ( property ) {
    case se_property_f32_m:
    case se_property_u32_m:
    case se_property_i32_m:
        return 4;
    case se_property_u64_m:
    case se_property_i64_m:
    case se_property_2f32_m:
        return 8;
    case se_property_3f32_m:
        return 12;
    case se_property_4f32_m:
        return 16;
    case se_property_string_m:
        return se_property_string_max_size_m;
    }

    std_log_error_m ( "Unknown property type" );
    return 0;
}

void se_entity_property_set ( se_property_h property_handle, void* data ) {
    se_entity_component_properties_t* component_props = &se_entity_state->component_meta->property_array[property_handle.component];
    se_property_t* property = &component_props->properties[property_handle.property];
    
    se_entity_t* entity = &se_entity_state->entity_array[property_handle.entity];
    se_entity_family_t* family = &se_entity_state->family_array[entity->family];
    uint8_t slot = family->component_slots[property_handle.component];
    se_entity_family_stream_t* stream = &family->components[slot].streams[property->stream];

    void* dst_data = se_entity_family_get_component_stream_data ( entity, stream );
    dst_data += property->offset;
    uint32_t stride = se_entity_property_stride ( property->type );

    if ( property->type != se_property_string_m ) { 
        std_mem_copy ( dst_data, data, stride );
    } else {
        std_str_copy ( dst_data, se_property_string_max_size_m, data );
    }
}

#if 0
// TODO remove
void se_entity_update ( const se_entity_update_t* updates, uint64_t update_count ) {
    for ( uint64_t update_it = 0; update_it < update_count; ++update_it ) {
        se_entity_update_t* update = &updates[update_it];
        se_entity_t* entity = &se_entity_state->entity_array[update->entity];
        se_entity_family_t* family = se_entity_family_get ( entity->mask );
        uint32_t family_idx = entity->family_idx;

        for ( uint32_t i = 0; i < update->component_count; ++i ) {
            se_component_t* component = &update->components[i];
            
            uint32_t id = component->id;
            std_assert_m ( id < se_max_component_types_m );

            uint8_t family_slot = family->component_slots[id];
            std_assert_m ( family_slot < se_entity_max_components_m );
            se_entity_family_component_t* family_component = family->components[family_slot];

            for ( uint32_t j = 0; j < component->stream_count; ++j ) {
                se_component_stream_t* stream = &component->streams[j];

                se_entity_family_stream_t* family_stream = family_component->streams[stream->id];

                uint32_t stride = family_stream->stride;
                uint32_t items_per_page = family_stream->items_per_page;
                uint32_t page_idx = family_idx / items_per_page;
                uint32_t page_sub_idx = family_idx % items_per_page;

                std_mem_copy ( family_stream->pages[page_idx] + page_sub_idx * stride, stream->data, stride );
            }
        }
    }
}
#endif
