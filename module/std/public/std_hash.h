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

#define std_hash_32_m(value) std_hash_murmur_mixer_32(value)
#define std_hash_64_m(value) std_hash_murmur_mixer_64(value)

//
// Custom hash/cmp functions makes possible to e.g. store idx to keys/payloads and hash/compare those by passing the base as custom arg.
// map_lookup returns a pointer to the payload, or NULL on miss.
// you should NOT store pointers to payloads for a long time, as those can get moved around on any remove
// remove takes a payload ptr, not a key, and assumes that the payload provided is valid (the map contains a valid key for it)
//
// TODO rework to not use any fun ptr
//
#if 0
typedef bool        ( std_map_cmp_f )  ( uint64_t hash1, const void* key1, const void* key2, void* usr_arg );
typedef uint64_t    ( std_map_hash_f ) ( const void* key, void* usr_arg );
typedef struct {
    void* keys;
    void* payloads;
    size_t pop;
    size_t mask;
    std_map_hash_f* hasher;
    std_map_cmp_f* cmp;
    void* hash_arg;
    void* cmp_arg;
    size_t key_stride;
    size_t payload_stride;
} std_map_t;

// NOTE: The capacity for the map is determined at the min value between keys and payloads buffers capacity.
//       This value is expected to be a power of two. If not, the capacity goes down to the closes pow2 and
//       a warning is produced.
std_map_t                       std_map          ( void* keys, void* payloads, size_t key_stride, size_t payload_stride, size_t capacity, 
                                                    std_map_hash_f* key_hash, void* hash_arg, std_map_cmp_f* key_cmp, void* cmp_arg );
std_map_t                       std_map_u64      ( std_buffer_t keys, std_buffer_t payloads );
void*                           std_map_insert   ( std_map_t* map, const void* key, const void* payload );  // returns pointer to payload copy
void                            std_map_remove   ( std_map_t* map, const void* payload );
void*                           std_map_lookup   ( const std_map_t* map, const void* key );
const void*                     std_map_get_key  ( const std_map_t* map, const void* payload );
void                            std_map_clear    ( std_map_t* map );

bool std_map_cmp_u64 ( uint64_t hash1, const void* key1, const void* key2, void* unused );
uint64_t std_map_hasher_u64 ( const void* key, void* unused );

#define std_static_map_m( keys, payloads, hasher, hasher_arg, cmp, cmp_arg ) \
    std_map ( std_static_buffer_m ( keys ), std_static_buffer_m ( payloads ), sizeof( *keys ), sizeof( *payloads ), \
    hasher, hasher_arg, cmp, cmp_arg )
#endif

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
bool                std_hash_map_remove ( std_hash_map_t* map, uint64_t hash ); // TODO rename std_hash_map_remove_hash
uint64_t*           std_hash_map_lookup ( std_hash_map_t* map, uint64_t hash ); // TODO split into _edit (returns u64 ptr) and _get(?) (returns u64 value)
bool                std_hash_map_remove_payload ( std_hash_map_t* map, uint64_t* payload );

#define std_static_hash_map_m( keys, payloads ) std_hash_map ( std_static_buffer_m ( keys ), std_static_buffer_m ( payloads ) )

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
bool                std_hash_set_insert ( std_hash_set_t* set, uint64_t hash );
bool                std_hash_set_remove ( std_hash_set_t* set, uint64_t hash );
bool                std_hash_set_lookup ( std_hash_set_t* set, uint64_t hash );

// TODO https://github.com/RonPieket/BinaryRelations
