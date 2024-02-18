#pragma once

#include <se.h>

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
    size_t mask;
    uint64_t keys[32];
    uint64_t payloads[32];
} se_entity_component_table_t;
#endif

void se_entity_init ( void );

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
size_t se_entity_extract ( se_component_flags_t* out_component_flags, se_entity_h* out_entity_handles );
