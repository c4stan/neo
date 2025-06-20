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
        state->page_array[i] = std_virtual_heap_alloc_m ( se_entity_family_page_size_m, 16 );
    }
    state->page_count = se_entity_family_page_count_m;

    state->entity_array = std_virtual_heap_alloc_array_m ( se_entity_t, se_max_entities_m );
    state->entity_freelist = std_freelist_m ( state->entity_array, se_max_entities_m );

    state->entity_destroy_array = std_virtual_heap_alloc_array_m ( se_entity_h, se_max_entities_m );
    state->entity_destroy_count = 0;

    std_mutex_init ( &se_entity_state->mutex );

    state->entity_meta = std_virtual_heap_alloc_struct_m ( se_entity_metadata_t );
    state->component_meta = std_virtual_heap_alloc_struct_m ( se_entity_component_metadata_t );
    std_mem_zero_m ( state->entity_meta );
    std_mem_zero_m ( state->component_meta );
}

void se_entity_reload ( se_entity_state_t* state ) {
    se_entity_state = state;
}

void se_entity_unload ( void ) {
    std_virtual_heap_free ( se_entity_state->family_mask_array );
    std_virtual_heap_free ( se_entity_state->family_array );
    std_virtual_heap_free ( se_entity_state->family_map_hashes );
    std_virtual_heap_free ( se_entity_state->family_map_values );
    for ( uint64_t i = 0; i < se_entity_family_page_count_m; ++i ) {
        std_virtual_heap_free ( se_entity_state->page_array[i] );
    }
    std_virtual_heap_free ( se_entity_state->page_array );
    std_virtual_heap_free ( se_entity_state->entity_array );
    std_virtual_heap_free ( se_entity_state->entity_destroy_array );
    std_virtual_heap_free ( se_entity_state->entity_meta );
    std_virtual_heap_free ( se_entity_state->component_meta );

    std_mutex_deinit ( &se_entity_state->mutex );
}

static uint64_t se_entity_component_mask_hash ( se_component_mask_t mask ) {
    uint64_t hash = std_hash_block_64_m ( mask.u64, se_component_mask_block_count_m * sizeof ( uint64_t ) );
    return hash;
}

void se_entity_family_create ( const se_entity_family_params_t* params ) {
    uint32_t component_count = params->component_count;

    se_entity_family_t* family = std_list_pop_m ( &se_entity_state->family_freelist );

    family->component_count = component_count;
    family->entity_count = 0;
    std_mem_set ( family->component_slots, sizeof ( family->component_slots ), 0xff );

    se_component_mask_t mask;
    std_mem_zero_static_array_m ( mask.u64 );

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

static void* se_entity_family_get_component ( se_entity_family_t* family, uint32_t entity_idx, uint32_t component_id, uint8_t stream_id ) {
    std_assert_m ( component_id < se_max_component_types_m );
    uint8_t component_family_slot = family->component_slots[component_id];
    if ( component_family_slot == 0xff ) {
        return NULL;
    }
    std_assert_m ( component_family_slot < se_max_components_per_entity_m );
    se_entity_family_component_t* family_component = &family->components[component_family_slot];
    se_entity_family_stream_t* family_stream = &family_component->streams[stream_id];

    uint32_t stride = family_stream->stride;
    uint32_t items_per_page = family_stream->items_per_page;
    uint32_t page_idx = entity_idx / items_per_page;
    uint32_t page_sub_idx = entity_idx % items_per_page;

    void* data = family_stream->pages[page_idx] + page_sub_idx * stride;
    return data;
}

// idx is entity->idx field
static void* se_entity_family_get_stream_data ( se_entity_family_stream_t* stream, uint32_t idx  ) {
    uint32_t stride = stream->stride;
    uint32_t items_per_page = stream->items_per_page;
    uint32_t page_idx = idx / items_per_page;
    uint32_t page_sub_idx = idx % items_per_page;   

    return stream->pages[page_idx] + page_sub_idx * stride;
}

static void se_entity_family_move_stream_data ( se_entity_family_stream_t* stream, uint32_t from, uint32_t to ) {
    void* src = se_entity_family_get_stream_data ( stream, from );
    void* dst = se_entity_family_get_stream_data ( stream, to );
    std_mem_copy ( dst, src, stream->stride );
}

void se_entity_alloc_components ( se_entity_h entity_handle, se_component_mask_t mask ) {
    se_entity_t* entity = &se_entity_state->entity_array[entity_handle];
    se_entity_family_t* family = se_entity_family_get ( mask );
#if std_log_error_enabled_m
    if ( !family ) {
        char buffer[64 * se_component_mask_block_count_m];
        for ( uint32_t i = 0; i < se_component_mask_block_count_m; ++i ) {
            std_u64_to_bin ( mask.u64[se_component_mask_block_count_m - i - 1], buffer + 64 * i );
        }
        std_log_error_m ( "Missing family " std_fmt_str_m, buffer );
    }
#endif

    //entity->mask = mask;
    entity->family = family - se_entity_state->family_array;
    uint32_t family_idx = family->entity_count++;
    entity->idx = family_idx;

    // allocate entity
    {
        se_entity_family_stream_t* stream = &family->entity_stream;

        uint32_t items_per_page = stream->items_per_page;
        uint32_t page_idx = family_idx / items_per_page;

        while ( stream->page_count < page_idx + 1 ) {
            stream->pages[stream->page_count++] = se_entity_state->page_array[--se_entity_state->page_count];
        }

        // copy entity handle
        uint32_t page_sub_idx = family_idx % items_per_page;
        uint32_t stride = stream->stride;
        std_assert_m ( sizeof ( entity_handle ) == stride );
        std_mem_copy ( stream->pages[page_idx] + stride * page_sub_idx, &entity_handle, stride );
    }

    // allocate component pages
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
    return ( se_entity_h ) entity_idx;
}

void se_entity_update ( se_entity_h entity_handle,  const se_entity_update_t* update ) {
    se_entity_t* entity = &se_entity_state->entity_array[entity_handle];
    se_entity_family_t* family = &se_entity_state->family_array[entity->family];
    uint32_t family_idx = entity->idx;

    for ( uint32_t i = 0; i < update->component_count; ++i ) {
        const se_component_update_t* component = &update->components[i];
        
        uint32_t id = component->id;
        std_assert_m ( id < se_max_component_types_m );

        uint8_t family_slot = family->component_slots[id];
        std_assert_m ( family_slot < se_max_components_per_entity_m );
        se_entity_family_component_t* family_component = &family->components[family_slot];

        for ( uint32_t j = 0; j < component->stream_count; ++j ) {
            const se_stream_update_t* stream = &component->streams[j];

            se_entity_family_stream_t* family_stream = &family_component->streams[stream->id];

            uint32_t stride = family_stream->stride;
            uint32_t items_per_page = family_stream->items_per_page;
            uint32_t page_idx = family_idx / items_per_page;
            uint32_t page_sub_idx = family_idx % items_per_page;

            std_mem_copy ( family_stream->pages[page_idx] + page_sub_idx * stride, stream->data, stride );
        }
    }
}

#if 0
static void se_entity_debug_print ( void ) {
    std_log_info_m ( "------" ); 
    uint64_t idx = 0;
    while ( std_bitset_scan ( &idx, se_entity_state->entity_meta->used_entities, idx, std_bitset_u64_count_m ( se_max_entities_m ) ) ) {
        se_entity_name_t* name = &se_entity_state->entity_meta->names[idx];
        se_entity_t* entity = &se_entity_state->entity_array[idx];
        std_log_info_m ( "handle: " std_fmt_u64_m ", famiy: " std_fmt_u32_m ", idx: " std_fmt_u32_m ", name: " std_fmt_str_m, 
            idx, entity->family, entity->idx, name->string );
        ++idx;
    }
}
#endif

se_entity_h se_entity_create_init ( const se_entity_params_t* params ) {
    se_component_mask_t mask = {};
    for ( uint32_t i = 0; i < params->update.component_count; ++i ) {
        uint32_t id = params->update.components[i].id;
        std_assert_m ( id < se_max_component_types_m );
        std_bitset_set ( mask.u64, id );
    }
    se_entity_h entity = se_entity_reserve();
    se_entity_alloc_components ( entity, mask );
    se_entity_update ( entity, &params->update );
    se_entity_set_name ( entity, params->debug_name );
    return entity;
}

void se_entity_destroy ( const se_entity_h entity_handle ) {
    se_entity_t* entity = &se_entity_state->entity_array[entity_handle];
    se_entity_family_t* family = &se_entity_state->family_array[entity->family];
    uint32_t entity_idx = entity->idx;
    uint32_t swap_idx = family->entity_count - 1;
    if ( swap_idx != entity_idx ) {
        se_entity_h* family_entity_handle_ptr = se_entity_family_get_stream_data ( &family->entity_stream, entity_idx );
        se_entity_h* swap_entity_handle_ptr = se_entity_family_get_stream_data ( &family->entity_stream, swap_idx );
        se_entity_h swap_entity_handle = *swap_entity_handle_ptr;
        *family_entity_handle_ptr = swap_entity_handle;
        se_entity_t* swap_entity = &se_entity_state->entity_array[swap_entity_handle];
        swap_entity->idx = entity->idx;

        for ( uint32_t i = 0; i < family->component_count; ++i ) {
            se_entity_family_component_t* family_component = &family->components[i];
            for ( uint32_t j = 0; j < family_component->stream_count; ++j ) {
                se_entity_family_move_stream_data ( &family_component->streams[j], swap_idx, entity_idx );
            }
        }
    }
    family->entity_count -= 1;
    std_bitset_clear ( se_entity_state->entity_meta->used_entities, entity_handle );
}

const char* se_entity_name ( se_entity_h entity_handle ) {
    se_entity_name_t* name = &se_entity_state->entity_meta->names[entity_handle];
    return name->string;
}

static void se_entity_extract_stream ( se_data_stream_t* result, const se_entity_family_stream_t* stream, uint32_t entity_count ) {
    if ( entity_count == 0 ) {
        return;
    }

    uint32_t capacity = stream->items_per_page;
    
    result->page_capacity = capacity;
    result->data_stride = stream->stride;

    uint32_t base = result->page_count;
    uint32_t count = 0;

    for ( uint32_t k = 0; k < stream->page_count; ++k ) {
        result->pages[base + k].data = stream->pages[k];
        result->pages[base + k].count += count + capacity <= entity_count ? capacity : entity_count - count;
        
        count += capacity;
    }

    result->page_count += stream->page_count;
    std_assert_m ( result->page_count <= se_entity_family_max_pages_per_stream_m );
}

void se_entity_query ( se_query_result_t* result, const se_query_params_t* params ) {
    // fill query mask from query components
    se_component_mask_t query_mask;
    std_mem_zero_m ( &query_mask );

    for ( uint32_t i = 0; i < params->component_count; ++i ) {
        std_bitset_set ( query_mask.u64, params->components[i] );
    }

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

        for ( uint32_t i = 0; i < params->component_count; ++i ) {
            uint32_t component_id = params->components[i];
            uint8_t family_slot = family->component_slots[component_id];
            std_assert_m ( family_slot != 0xff );
            se_entity_family_component_t* component = &family->components[family_slot];

            se_component_data_t* result_component = &result->components[i];
            
            result_component->stream_count = component->stream_count;

            for ( uint32_t j = 0; j < component->stream_count; ++j ) {
                #if 0
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
                #else
                se_entity_extract_stream ( &result_component->streams[j], &component->streams[j], entity_count );
                #endif
            }
        }

        // TODO make this conditional? have separate APIs for components and components+entities?
        se_entity_extract_stream ( &result->entities, &family->entity_stream, entity_count );

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

void se_entity_property_get ( se_entity_properties_t* out_props, se_entity_h entity_handle ) {
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
    case se_property_bool_m:
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

    void* dst_data = se_entity_family_get_stream_data ( stream, entity->idx );
    dst_data += property->offset;
    uint32_t stride = se_entity_property_stride ( property->type );

    if ( property->type != se_property_string_m ) { 
        std_mem_copy ( dst_data, data, stride );
    } else {
        std_str_copy ( dst_data, se_property_string_max_size_m, data );
    }
}
