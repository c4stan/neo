#pragma once

#include <std_platform.h>

uint32_t std_hash_murmur_32 ( uint32_t value );
uint64_t std_hash_murmur_64 ( uint64_t value );
uint32_t std_hash_fnv1a_block_32 ( const void* base, uint32_t size );
uint64_t std_hash_fnv1a_block_64 ( const void* base, uint32_t size );
uint32_t std_hash_fnv1a_string_32 ( const char* str );
uint64_t std_hash_fnv1a_string_64 ( const char* str );

#define std_hash_32_m( value ) std_hash_murmur_32 ( value )
#define std_hash_64_m( value ) std_hash_murmur_64 ( value )
#define std_hash_block_32_m( base, size ) std_hash_fnv1a_block_32 ( base, size )
#define std_hash_block_64_m( base, size ) std_hash_fnv1a_block_64 ( base, size )
#define std_hash_string_32_m( str ) std_hash_fnv1a_string_32 ( str )
#define std_hash_string_64_m( str ) std_hash_fnv1a_string_64 ( str )

// Linear probing hash map. Only hashes are stored, no guarantees against external hash collisions
typedef struct {
    uint64_t* hashes;
    uint64_t* payloads;
    size_t count;
    size_t mask;
} std_hash_map_t;

std_hash_map_t      std_hash_map ( uint64_t* keys, uint64_t* values, size_t capacity ); // Clears keys, doesn't touch values
bool                std_hash_map_insert ( std_hash_map_t* map, uint64_t hash, uint64_t payload ); // Returns false if hash is already present
bool                std_hash_map_try_insert ( uint64_t* out_payload, std_hash_map_t* map, uint64_t hash, uint64_t payload ); // Returns false and writes out payload if already present
uint64_t*           std_hash_map_lookup ( std_hash_map_t* map, uint64_t hash ); // Returns NULL if hash is not present
bool                std_hash_map_remove_hash ( std_hash_map_t* map, uint64_t hash );
bool                std_hash_map_remove_payload ( std_hash_map_t* map, uint64_t* payload );
std_hash_map_t      std_hash_map_create ( uint64_t capacity ); // Heap allocated
void                std_hash_map_destroy ( std_hash_map_t* map ); // Call on heap allocated hash maps only

// NOTE: This allows atomic insertions form multiple threads but it does NOT support a number of other operations happening in parallel with the insertions:
//      - Removal of any item in the map is NOT supported
//      - Lookup is supported ONLY for already existing items. Lookup for the specific item that's being inserted is NOT supported.
//      - Access to the 'count' field is NOT supported. Reason being that it is NOT updated atomically with the insertion.
bool                std_hash_map_insert_shared ( std_hash_map_t* map, uint64_t hash, uint64_t payload );

#define std_static_hash_map_m( keys, payloads ) std_hash_map ( keys, payloads, std_static_array_capacity_m ( keys ) )

// Hash set, same as hash_map but doesn't store payloads
typedef struct {
    uint64_t* hashes;
    size_t count;
    size_t mask;
} std_hash_set_t;

std_hash_set_t      std_hash_set ( uint64_t* hashes, size_t capacity );
bool                std_hash_set_insert ( std_hash_set_t* set, uint64_t hash ); // Returns false if hash is already present
bool                std_hash_set_remove ( std_hash_set_t* set, uint64_t hash );
bool                std_hash_set_lookup ( std_hash_set_t* set, uint64_t hash );
void                std_hash_set_clear ( std_hash_set_t* set );

#define std_static_hash_set_m( array ) std_hash_set ( array, std_static_array_capacity_m ( array ) )
