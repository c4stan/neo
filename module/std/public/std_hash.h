#pragma once

#include <std_allocator.h>
#include <std_platform.h>

uint32_t    std_hash_murmur_mixer_32 ( uint32_t value );
uint64_t    std_hash_murmur_mixer_64 ( uint64_t value );

uint32_t    std_hash_murmur_32 ( const void* base, uint32_t size );

// TODO: is it ok to use djb2 on generic data? as in not necessarily textual strings
uint32_t    std_hash_djb2_32 ( const void* base, size_t size );
uint64_t    std_hash_djb2_64 ( const void* base, size_t size );

// https://github.com/jandrewrogers/MetroHash
// TODO this is broken?
uint64_t    std_hash_metro ( const void* base, size_t size );

typedef struct {
    uint64_t v[4];
    uint8_t b[32];
    uint64_t vseed;
    uint64_t bytes;
} std_hash_metro_state_t;

void        std_hash_metro_begin ( std_hash_metro_state_t* state );
void        std_hash_metro_add ( std_hash_metro_state_t* state, const void* base, size_t size );
uint64_t    std_hash_metro_end ( std_hash_metro_state_t* state );

#define std_hash_metro_add_m( state, item ) std_hash_metro_add ( state, item, sizeof ( *(item) ) )

#define std_hash_32_m( value ) std_hash_murmur_mixer_32(value)
#define std_hash_64_m( value ) std_hash_murmur_mixer_64(value)

// Differently from std_map, std_hash_map doesn't store the original key value, and only works with hashes
// The hash function is expected to be handled from outside and there's no internal guard against 2 different keys
// having the same hash
// TODO allow for custom payload stride?
typedef struct {
    uint64_t* hashes;
    uint64_t* payloads;
    size_t count;
    size_t mask;
} std_hash_map_t;

// Clears keys, doesn't touch values
std_hash_map_t      std_hash_map ( uint64_t* keys, uint64_t* values, size_t capacity );
bool                std_hash_map_insert ( std_hash_map_t* map, uint64_t hash, uint64_t payload );
bool                std_hash_map_try_insert ( uint64_t* out_payload, std_hash_map_t* map, uint64_t hash, uint64_t payload ); // Returns false and writes out payload if already present
bool                std_hash_map_remove ( std_hash_map_t* map, uint64_t hash ); // TODO rename std_hash_map_remove_hash
uint64_t*           std_hash_map_lookup ( std_hash_map_t* map, uint64_t hash ); // TODO split into _edit (returns u64 ptr) and _get(?) (returns u64 value)
bool                std_hash_map_remove_payload ( std_hash_map_t* map, uint64_t* payload );
std_hash_map_t      std_hash_map_create ( uint64_t capacity );
void                std_hash_map_destroy ( std_hash_map_t* map );

#define std_static_hash_map_m( keys, payloads ) std_hash_map ( keys, payloads, std_static_array_capacity_m ( keys ) )

// NOTE: This allows atomic insertions form multiple threads but it does NOT support a number of other operations happening in parallel with the insertions:
//      - Removal of any item in the map is NOT supported
//      - Lookup is supported ONLY for already existing items. Lookup for the specific item that's being inserted is NOT supported.
//      - Access to the 'count' field is NOT supported. Reason being that it is NOT updated atomically with the insertion.
bool    std_hash_map_insert_shared ( std_hash_map_t* map, uint64_t hash, uint64_t payload );

/* template begin
def <T, S, P>
typedef struct {
    $T* hashes;
    $T* payloads;
    $S count;
    $S mask;
} std_hash_map_$P_t

std_hash_map_$P_t std_hash_map_$P ( std_buffer_t keys, std_buffer_t values );
bool std_hash_map_$P_insert ( std_hash_map_$P_t* map, $T hash, $T payload );
void std_hash_map_$P_remove ( std_hash_map_$P_t* map, $T hash, $T payload );
$T* std_hash_map_$P_lookup ( std_hash_map_$P_t* map, $T hash );

make <uint32_t, uint32_t, 32>
*/

// Hash set, similar to hash_map but doesn't store payloads
typedef struct {
    uint64_t* hashes;
    size_t count;
    size_t mask;
} std_hash_set_t;

std_hash_set_t      std_hash_set ( uint64_t* hashes, size_t capacity );
bool                std_hash_set_insert ( std_hash_set_t* set, uint64_t hash ); // Returns false if hash is found
bool                std_hash_set_remove ( std_hash_set_t* set, uint64_t hash );
bool                std_hash_set_lookup ( std_hash_set_t* set, uint64_t hash );
void                std_hash_set_clear ( std_hash_set_t* set );

#define std_static_hash_set_m( array ) std_hash_set ( array, std_static_array_capacity_m ( array ) )

// TODO https://github.com/RonPieket/BinaryRelations
