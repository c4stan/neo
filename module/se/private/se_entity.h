#pragma once

#include <se.h>

#include <std_allocator.h>
#include <std_mutex.h>
#include <std_hash.h>

// TODO use virtual arrays instead of this paged chunk system?
//      only downside seems to be losing the potential (not yet implemented) to free
//      entire pages at once instead of having to always delete entities one by one.
//      could be useful when it comes down to e.g. static entities in a level?
typedef struct {
    uint32_t stride;
    uint32_t page_count;
    uint32_t items_per_page;
    void* pages[se_entity_family_max_pages_per_stream_m];    
} se_entity_family_stream_t;

typedef struct {
    se_component_e id;
    // The implementation assumes that streams don't have gaps, e.g. if count is declared as n then all streams from 0 to n-1 are assumed to be in use
    uint32_t stream_count; 
    se_entity_family_stream_t streams[se_component_max_streams_m];
} se_entity_family_component_t;

// for component_slots
std_static_assert_m ( se_max_components_per_entity_m < UINT8_MAX );

typedef struct {
    //se_component_mask_t component_mask;
    uint32_t entity_count; // count of total entities alive for this family. Each entity has a set of components associated to it.
    uint32_t component_count; // count of component types used by this entity family. set at family creation time.
    se_entity_family_stream_t entity_stream; // stores handles to the entities that own the components
    uint8_t component_slots[se_max_component_types_m]; // component type id -> idx in components array. TODO replace with hash_map?
    se_entity_family_component_t components[se_max_components_per_entity_m];
} se_entity_family_t;

#if 0
typedef struct {
    uint32_t count;
    uint32_t mask; // capacity == mask + 1
    // TODO make storing keys optional? is it possible to perfect hash the types while supporting different table sizes?

    // TODO
    //uint32_t keys[];
    //uint32_t payloads[];
    uint32_t keys[32];
    uint32_t payloads[32];
} se_entity_component_table_t;
#else
typedef struct {
    size_t count;
    size_t mask; // TODO no need to store this, just derive from arrays static size...
    uint64_t keys[32]; // TODO replace 32 with max_components_per_entity_type macro
    uint64_t payloads[32];
} se_entity_component_table_t;
#endif

//void se_entity_init ( void );

typedef struct {
    //se_component_mask_t mask;
    uint32_t family; // family idx
    uint32_t idx; // entity idx in the family
} se_entity_t;

#define std_entity_family_bitset_block_count_m ( std_div_ceil_m(se_entity_max_families_m, 64) )

// TODO make this debug/tool only
#if 0
typedef struct {
    uint32_t property_count;
    se_property_t properties[se_component_max_properties_m];
} se_entity_stream_properties_t;

typedef struct {
    se_entity_stream_properties_t streams[se_component_max_streams_m];
} se_entity_component_properties_t;
#endif
typedef struct {
    char string[se_debug_name_size_m];
} se_entity_name_t;

typedef struct {
    uint32_t property_count;
    char name[se_debug_name_size_m];
    se_property_t properties[se_component_max_properties_m];
} se_entity_component_properties_t;

typedef struct {
    // TODO freelist + map? dynamic grow?
    se_entity_component_properties_t property_array[se_max_component_types_m];
} se_entity_component_metadata_t;

typedef struct {
    uint64_t used_entities[ std_div_ceil_m ( se_max_entities_m, 64 ) ];    
    se_entity_name_t names[se_max_entities_m];
} se_entity_metadata_t;

typedef struct {
    std_mutex_t mutex;

    uint64_t family_bitset[std_entity_family_bitset_block_count_m];
    se_component_mask_t* family_mask_array;
    se_entity_family_t* family_array;
    se_entity_family_t* family_freelist;
    uint64_t* family_map_hashes;
    uint64_t* family_map_values;
    std_hash_map_t family_map;

    void** page_array;
    uint64_t page_count;

    se_entity_t* entity_array;
    se_entity_t* entity_freelist;

    se_entity_h* entity_destroy_array;
    uint64_t entity_destroy_count;

    se_entity_metadata_t* entity_meta;
    se_entity_component_metadata_t* component_meta;
} se_entity_state_t;

void se_entity_load ( se_entity_state_t* state );
void se_entity_reload ( se_entity_state_t* state );
void se_entity_unload ( void );

void se_entity_family_create ( const se_entity_family_params_t* params );
void se_entity_family_destroy ( se_component_mask_t mask );

se_entity_h se_entity_reserve ( void );

void se_entity_update ( se_entity_h entity, const se_entity_update_t* update );
se_entity_h se_entity_create_init ( const se_entity_params_t* params );

//void se_entity_create ( const se_entity_params_t* params );
void se_entity_destroy ( const se_entity_h entity_handle );

void se_entity_query ( se_query_result_t* result, const se_query_params_t* params );

void* se_entity_get_component ( se_entity_h entity_handle, se_component_e component, uint8_t stream );

const char* se_entity_name ( se_entity_h entity_handle );
void se_entity_set_name ( se_entity_h entity, const char* name );
void se_entity_set_component_properties ( se_component_e component, const char* name, const se_component_properties_params_t* params );
size_t se_entity_list ( se_entity_h* out_entities, size_t cap );
void se_entity_property_get ( se_entity_properties_t* out_props, se_entity_h entity_handle );

#if 0
se_entity_h se_entity_create ( const se_entity_params_t* params );
//bool se_entity_create_array ( se_entity_h* entities, const se_entity_params_t* params, size_t count );
bool se_entity_destroy ( se_entity_h entity );
//bool se_entity_destroy_array ( const se_entity_h* entities, size_t count );

/*
se_entity memory layout
	serialized scene
		entities grouped by lifetime when possible
			all entities in the group can be allocated in a segment,
		entities flagged as static component layout when possible

*/

// TODO what's the synchronization guarantee needed for component add/remove?
bool se_entity_component_add ( se_entity_h entity, se_component_e component_type, se_component_h component_handle );
bool se_entity_component_remove ( se_entity_h entity, se_component_e component_type );
se_component_h se_entity_component_get ( se_entity_h entity, se_component_e component_type );
void se_entity_component_get_array ( se_component_h* out_component_handles, size_t count, const se_entity_h* entities, se_component_e component_type );

// TODO allocate virtual array internally and only commit used memory
size_t se_entity_extract ( se_component_mask_t* out_component_masks, se_entity_h* out_entity_handles );
#else

#endif
