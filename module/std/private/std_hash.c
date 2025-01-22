#include <std_hash.h>

#include <std_compiler.h>
#include <std_log.h>
#include <std_atomic.h>
#include <std_allocator.h>

// ------------------------------------------------------------------------------------------------------
// Hash
// ------------------------------------------------------------------------------------------------------
// https://github.com/aappleby/smhasher/blob/master/src/MurmurHash3.cpp
uint32_t std_hash_murmur_32 ( uint32_t h ) {
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;

    return h;
}

uint64_t std_hash_murmur_64 ( uint64_t k ) {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdLLU;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53LLU;
    k ^= k >> 33;

    return k;
}

uint32_t std_hash_fnv1a_block_32 ( const void* base, uint32_t size ) {
    unsigned int hash = 0x811C9DC5;
    unsigned int prime = 0x01000193;
    unsigned char* data = ( unsigned char* ) base;

    for ( uint32_t i = 0; i < size; ++i ) {
        hash ^= data[i];
        hash *= prime;
    }

    return hash;
}

uint64_t std_hash_fnv1a_block_64 ( const void* base, uint32_t size ) {
    uint64_t hash = 0xCBF29CE484222325;
    uint64_t prime = 0x100000001B3;
    unsigned char* data = ( unsigned char* ) base;

    for ( uint32_t i = 0; i < size; ++i ) {
        hash ^= data[i];
        hash *= prime;
    }

    return hash;
}

uint32_t std_hash_fnv1a_string_32 ( const char* data ) {
    unsigned int hash = 0x811C9DC5;
    unsigned int prime = 0x01000193;

    while ( *data ) {
        hash ^= ( unsigned char )( *data );
        hash *= prime;
        data++;
    }

    return hash;
}

uint64_t std_hash_fnv1a_string_64 ( const char* data ) {
    uint64_t hash = 0xCBF29CE484222325;
    uint64_t prime = 0x100000001B3;

    while ( *data ) {
        hash ^= ( unsigned char ) ( *data );
        hash *= prime;
        data++;
    }

    return hash;
}

uint32_t std_hash_djb2_32 ( const void* base, size_t size ) {
    uint32_t hash = 5381;

    for ( size_t i = 0; i < size; ++i ) {
        uint32_t c = ( ( const char* ) base ) [i];
        hash = hash * 33 ^ c;
    }

    return hash;
}

uint64_t std_hash_djb2_64 ( const void* base, size_t size ) {
    uint64_t hash = 5381;

    for ( size_t i = 0; i < size; ++i ) {
        uint64_t c = ( ( const char* ) base ) [i];
        hash = hash * 33 ^ c;
    }

    return hash;
}

#if 0
static uint64_t std_hash_metro_rotate_right ( uint64_t v, uint32_t k ) {
    return ( v >> k ) | ( v << ( 64 - k ) );
}

static uint64_t std_hash_metro_read_u64 ( const void* ptr ) {
    return * ( uint64_t* ) ( ptr );
}

static uint32_t std_hash_metro_read_u32 ( const void* ptr ) {
    return * ( uint32_t* ) ( ptr );
}

static uint16_t std_hash_metro_read_u16 ( const void* ptr ) {
    return * ( uint16_t* ) ( ptr );
}

static uint8_t std_hash_metro_read_u8 ( const void* ptr ) {
    return * ( uint8_t* ) ( ptr );
}

// https://github.com/jandrewrogers/MetroHash
// https://github.com/jandrewrogers/MetroHash/blob/master/src/metrohash64.cpp
uint64_t std_hash_metro ( const void* base, size_t length ) {
    static const uint64_t k0 = 0xD6D018F5;
    static const uint64_t k1 = 0xA2AA033B;
    static const uint64_t k2 = 0x62992FC1;
    static const uint64_t k3 = 0x30BC5B29;

    uint64_t seed = 0;

    const uint8_t* ptr = ( const uint8_t* ) ( base );
    const uint8_t* const end = ptr + length;

    uint64_t h = ( seed + k2 ) * k0;

    if ( length >= 32 ) {
        uint64_t v[4];
        v[0] = h;
        v[1] = h;
        v[2] = h;
        v[3] = h;

        do {
            v[0] += std_hash_metro_read_u64 ( ptr ) * k0;
            ptr += 8;
            v[0] = std_hash_metro_rotate_right ( v[0], 29 ) + v[2];
            v[1] += std_hash_metro_read_u64 ( ptr ) * k1;
            ptr += 8;
            v[1] = std_hash_metro_rotate_right ( v[1], 29 ) + v[3];
            v[2] += std_hash_metro_read_u64 ( ptr ) * k2;
            ptr += 8;
            v[2] = std_hash_metro_rotate_right ( v[2], 29 ) + v[0];
            v[3] += std_hash_metro_read_u64 ( ptr ) * k3;
            ptr += 8;
            v[3] = std_hash_metro_rotate_right ( v[3], 29 ) + v[1];
        } while ( ptr <= ( end - 32 ) );

        v[2] ^= std_hash_metro_rotate_right ( ( ( v[0] + v[3] ) * k0 ) + v[1], 37 ) * k1;
        v[3] ^= std_hash_metro_rotate_right ( ( ( v[1] + v[2] ) * k1 ) + v[0], 37 ) * k0;
        v[0] ^= std_hash_metro_rotate_right ( ( ( v[0] + v[2] ) * k0 ) + v[3], 37 ) * k1;
        v[1] ^= std_hash_metro_rotate_right ( ( ( v[1] + v[3] ) * k1 ) + v[2], 37 ) * k0;
        h += v[0] ^ v[1];
    }

    if ( ( end - ptr ) >= 16 ) {
        uint64_t v0 = h + ( std_hash_metro_read_u64 ( ptr ) * k2 );
        ptr += 8;
        v0 = std_hash_metro_rotate_right ( v0, 29 ) * k3;
        uint64_t v1 = h + ( std_hash_metro_read_u64 ( ptr ) * k2 );
        ptr += 8;
        v1 = std_hash_metro_rotate_right ( v1, 29 ) * k3;
        v0 ^= std_hash_metro_rotate_right ( v0 * k0, 21 ) + v1;
        v1 ^= std_hash_metro_rotate_right ( v1 * k3, 21 ) + v0;
        h += v1;
    }

    if ( ( end - ptr ) >= 8 ) {
        h += std_hash_metro_read_u64 ( ptr ) * k3;
        ptr += 8;
        h ^= std_hash_metro_rotate_right ( h, 55 ) * k1;
    }

    if ( ( end - ptr ) >= 4 ) {
        h += std_hash_metro_read_u32 ( ptr ) * k3;
        ptr += 4;
        h ^= std_hash_metro_rotate_right ( h, 26 ) * k1;
    }

    if ( ( end - ptr ) >= 2 ) {
        h += std_hash_metro_read_u16 ( ptr ) * k3;
        ptr += 2;
        h ^= std_hash_metro_rotate_right ( h, 48 ) * k1;
    }

    if ( ( end - ptr ) >= 1 ) {
        h += std_hash_metro_read_u8 ( ptr ) * k3;
        h ^= std_hash_metro_rotate_right ( h, 37 ) * k1;
    }

    h ^= std_hash_metro_rotate_right ( h, 28 );
    h *= k0;
    h ^= std_hash_metro_rotate_right ( h, 29 );

    return h;
}
#endif

// ------------------------------------------------------------------------------------------------------
// Map
// ------------------------------------------------------------------------------------------------------
#if 0
std_map_t std_map ( std_buffer_t keys, std_buffer_t payloads, size_t key_stride, size_t payload_stride,
                    std_map_hash_f* hash, void* hash_arg, std_map_cmp_f* cmp, void* cmp_arg ) {
    std_map_t map;
    map.keys = keys.base;
    map.payloads = payloads.base;
    map.pop = 0;
    map.key_stride = key_stride;
    map.payload_stride = payload_stride;
    map.cmp = cmp;
    map.cmp_arg = cmp_arg;
    map.hasher = hash;
    map.hash_arg = hash_arg;

    size_t keys_cap = keys.size / key_stride;
    size_t payloads_cap = payloads.size / payload_stride;
    size_t min_cap = std_min ( keys_cap, payloads_cap );
    size_t cap = std_pow2_round_down ( min_cap );

    std_assert_m ( cap > 0 );
    map.mask = cap - 1;

    // Use u64_max as invalid key/hash.
    std_mem_set ( keys.base, keys.size, 0xff );
    return map;
}

uint64_t std_map_hasher_u64 ( const void* key_u64, void* unused ) {
    std_unused_m ( unused );
    uint64_t key = * ( uint64_t* ) key_u64;
    return std_hash_murmur_64 ( key );
}

bool std_map_cmp_u64 ( uint64_t hash1, const void* key1_u64, const void* key2_u64, void* unused ) {
    std_unused_m ( hash1 );
    std_unused_m ( unused );
    uint64_t key1 = * ( uint64_t* ) key1_u64;
    uint64_t key2 = * ( uint64_t* ) key2_u64;
    return key1 == key2;
}

std_map_t std_map_u64 ( std_buffer_t keys, std_buffer_t payloads ) {
    return std_map ( keys, payloads, sizeof ( uint64_t ), sizeof ( uint64_t ), std_map_hasher_u64, NULL, std_map_cmp_u64, NULL );
}

void* std_map_insert ( std_map_t* map, const void* key, const void* payload ) {
    // Load
    size_t      mask = map->mask;
    char*     keys = map->keys;
    size_t      key_stride = map->key_stride;
    char*     payloads = map->payloads;
    size_t      payload_stride = map->payload_stride;

    // Compute hash and offset
    uint64_t    hash = map->hasher ( key, map->hash_arg );
    size_t      idx = hash & mask;
    void*       map_key = keys + idx * key_stride;

    // Try insert until success (linear probing)
    for ( size_t i = 0; i < mask + 1; ++i ) {
        if ( std_mem_test ( map_key, key_stride, UINT8_MAX ) ) {
            void* map_payload = payloads + idx * payload_stride;
            std_mem_copy ( map_key, key, key_stride );
            std_mem_copy ( map_payload, payload, payload_stride );
            ++map->pop;
            return payloads + idx;
        }

        idx = ( idx + 1 ) & mask;
        map_key = keys + idx * key_stride;
    }

    std_log_error_m ( "map is full!" );
    return NULL;
}

void* std_map_lookup ( const std_map_t* map, const void* key ) {
    // Load
    size_t          mask = map->mask;
    const char*   keys = map->keys;
    size_t          key_stride = map->key_stride;
    char*         payloads = map->payloads;
    size_t          payload_stride = map->payload_stride;

    // Compute hash and offset
    uint64_t        hash = map->hasher ( key, map->hash_arg );
    size_t          idx = hash & mask;
    const void*     map_key = keys + idx * key_stride;

    // Try compare until success or empty slot (linear probing)
    for ( size_t i = 0; i < mask + 1; ++i ) {
        if ( std_mem_test ( map_key, key_stride, UINT8_MAX ) ) {
            // Empty
            return NULL;
        }

        if ( map->cmp ( hash, key, map_key, map->cmp_arg ) ) {
            // Success
            return payloads + idx * payload_stride;
        }

        idx = ( idx + 1 ) & mask;
        map_key = keys + idx * key_stride;
    }

    return NULL;
}

void std_map_remove ( std_map_t* map, const void* payload ) {
    // Load
    size_t      mask = map->mask;
    char*     keys = map->keys;
    size_t      key_stride = map->key_stride;
    char*     payloads = map->payloads;
    size_t      payload_stride = map->payload_stride;

    // Validate payload ptr
    std_assert_m ( ( char* ) payload >= payloads );
    std_assert_m ( ( char* ) payload < payloads + ( mask + 1 ) * payload_stride );

    // Compute key ptr
    size_t      offset = ( size_t ) ( ( char* ) payload - payloads );
    size_t      idx = offset / payload_stride;
    void*       map_key = keys + idx * key_stride;
    std_assert_m ( !std_mem_test ( map_key, key_stride, 0xff ) );

    // All elements that linearly follow the removed item need to be checked for
    // possible necessary reorder. The rule that all elements can be accessed by
    // hash lookup + linear probing must remain valid.
    size_t next_idx = idx;

    // This is just to ensure that at most we go through all elements in the map once.
    for ( size_t i = 0; i < mask + 1; ++i ) {
        next_idx = ( next_idx + 1 ) & mask;
        void* next_key = keys + next_idx * key_stride;

        // If element after is invalid, no more moving necessary. Can return.
        // If not ivalid, it might be victim of over crowding. Check and fix.
        if ( std_mem_test ( next_key, key_stride, 0xff ) ) {
            std_mem_set ( map_key, key_stride, 0xff );
            --map->pop;
            return;
        }

        // If next thinks remove idx is better that its idx, swap.
        uint64_t next_ideal_idx = map->hasher ( next_key, map->hash_arg ) & mask;
        uint64_t after_swap_cost = std_ring_distance_u64 ( next_ideal_idx, idx, mask + 1 );
        uint64_t curr_cost = std_ring_distance_u64 ( next_ideal_idx, next_idx, mask + 1 );

        if ( after_swap_cost < curr_cost ) {
            std_mem_copy ( map_key, next_key, key_stride );
            std_mem_copy ( payloads + idx * payload_stride, payloads + next_idx * payload_stride, payload_stride );
            // After swap, cell idx is now valid and cell next_idx is invalid, and the remove algorithm continues on next_idx.
            map_key = next_key;
            i += std_ring_distance_u64 ( idx, next_idx, mask + 1 );
            idx = next_idx;
        }
    }
}

const void* std_map_get_key ( const std_map_t* map, const void* payload ) {
    // Load
    size_t      key_stride = map->key_stride;
    size_t      payload_stride = map->payload_stride;
    size_t      mask = map->mask;
    char*     keys = map->keys;
    char*     payloads = map->payloads;

    // Validate payload pointer
    std_assert_m ( ( const char* ) payload >= payloads );
    std_assert_m ( ( const char* ) payload <= payloads + ( mask + 1 ) * payload_stride );

    // Compute key ptr
    size_t      offset = ( size_t ) ( ( const char* ) payload - payloads );
    size_t      idx = offset / payload_stride;
    const void* key = keys + idx * key_stride;
    std_assert_m ( !std_mem_test ( key, key_stride, 0xff ) );

    return key;
}

void std_map_clear ( std_map_t* map ) {
    map->pop = 0;
    std_mem_set ( map->keys, ( map->mask + 1 ) * map->key_stride, 0xff );
}
#endif
// ======================================================================================= //
//                                     H A S H   M A P
// ======================================================================================= //

std_hash_map_t std_hash_map_create ( uint64_t capacity ) {
    uint64_t* hashes = std_virtual_heap_alloc_array_m ( uint64_t, capacity );
    uint64_t* payloads = std_virtual_heap_alloc_array_m ( uint64_t, capacity );
    return std_hash_map ( hashes, payloads, capacity );
}

void std_hash_map_destroy ( std_hash_map_t* map ) {
    std_virtual_heap_free ( map->hashes );
    std_virtual_heap_free ( map->payloads );
}

std_hash_map_t std_hash_map ( uint64_t* hashes, uint64_t* payloads, size_t capacity ) {
    std_hash_map_t map;
    map.hashes = hashes;
    map.payloads = payloads;
    map.count = 0;
    map.mask = capacity - 1;

    for ( size_t i = 0; i < capacity; ++i ) {
        map.hashes[i] = UINT64_MAX;
    }

    return map;
}

bool std_hash_map_insert ( std_hash_map_t* map, uint64_t hash, uint64_t payload ) {
    // Load
    size_t      mask = map->mask;
    uint64_t*   hashes = map->hashes;
    uint64_t*   payloads = map->payloads;

    // Compute idx
    size_t      idx = hash & mask;

    // Try insert until success (linear probing)
    for ( size_t i = 0; i < mask + 1; ++i ) {
        if ( hashes[idx] == UINT64_MAX ) {
            hashes[idx] = hash;
            payloads[idx] = payload;
            ++map->count;
            return true;
        } else if ( hashes[idx] == hash ) {
            return false;
        }

        idx = ( idx + 1 ) & mask;
    }

    std_log_error_m ( "hash map is full!" );
    return false;
}

bool std_hash_map_try_insert ( uint64_t* out_payload, std_hash_map_t* map, uint64_t hash, uint64_t payload ) {
    // Load
    size_t      mask = map->mask;
    uint64_t*   hashes = map->hashes;
    uint64_t*   payloads = map->payloads;

    // Compute idx
    size_t      idx = hash & mask;

    // Try insert until success (linear probing)
    for ( size_t i = 0; i < mask + 1; ++i ) {
        if ( hashes[idx] == UINT64_MAX ) {
            hashes[idx] = hash;
            payloads[idx] = payload;
            ++map->count;
            return true;
        } else if ( hashes[idx] == hash ) {
            *out_payload = payloads[idx];
            return false;
        }

        idx = ( idx + 1 ) & mask;
    }

    std_log_error_m ( "hash map is full!" );
    return false;
}

bool std_hash_map_insert_shared ( std_hash_map_t* map, uint64_t hash, uint64_t payload ) {
    // Load
    size_t      mask = map->mask;
    uint64_t*   hashes = map->hashes;
    uint64_t*   payloads = map->payloads;

    // Compute idx
    size_t      idx = hash & mask;

    // Try insert until success (linear probing)
    spin:
    for ( uint64_t i = 0; i < mask + 1; ++i ) {
        uint64_t slot = hashes[idx];
        if ( slot == UINT64_MAX ) {
            if ( std_compare_and_swap_u64 ( &hashes[idx], &slot, hash ) ) {
                payloads[idx] = payload;
                std_atomic_increment_u64 ( &map->count );
                return true;
            } else {
                goto spin;
            }
        } else if ( slot == hash) {
            return false;
        }

        idx = ( idx + 1 ) & mask;
    }

    std_log_error_m ( "hash map is full!" );
    return false;
}

uint64_t* std_hash_map_lookup ( std_hash_map_t* map, uint64_t hash ) {
    // Load
    size_t          mask = map->mask;
    const uint64_t* hashes = map->hashes;
    uint64_t*       payloads = map->payloads;

    // Compute idx
    size_t          idx = hash & mask;
    uint64_t        map_hash = hashes[idx];

    // Try compare until success or empty slot (linear probing)
    for ( size_t i = 0; i < mask + 1; ++i ) {
        if ( map_hash == UINT64_MAX ) {
            // Empty
            return NULL;
        }

        if ( hash == map_hash ) {
            // Success
            return &payloads[idx];
        }

        idx = ( idx + 1 ) & mask;
        map_hash = hashes[idx];
    }

    return NULL;
}

static bool std_hash_map_remove_at_idx ( std_hash_map_t* map, size_t idx ) {
    // Load
    size_t      mask = map->mask;
    uint64_t*   hashes = map->hashes;
    uint64_t*   payloads = map->payloads;

    // On remove we try to move up all items that follow the removed one until an empty slot
    // is encountered. The rule that all elements can be accessed by hash lookup + linear probing
    // must remain valid.
    size_t next_idx = idx;

    // This is just to ensure that at most we go through all elements in the map once.
    for ( size_t i = 0; i < mask + 1; ++i ) {
        next_idx = ( next_idx + 1 ) & mask;
        uint64_t next_hash = hashes[next_idx];

        // If next element is invalid, no more moving necessary. Can return.
        if ( next_hash == UINT64_MAX ) {
            hashes[idx] = UINT64_MAX;
            --map->count;
            return true;
        }

        // Check if next wants to move up.
        uint64_t next_ideal_idx = next_hash & mask;
        uint64_t after_swap_cost = std_ring_distance_u64 ( next_ideal_idx, idx, mask + 1 );
        uint64_t curr_cost = std_ring_distance_u64 ( next_ideal_idx, next_idx, mask + 1 );

        if ( after_swap_cost < curr_cost ) {
            hashes[idx] = next_hash;
            payloads[idx] = payloads[next_idx];
            // After swap, cell idx is now valid and cell next_idx is invalid, and the remove algorithm continues on next_idx.
            i += std_ring_distance_u64 ( idx, next_idx, mask + 1 );
            idx = next_idx;
        }
    }

    // Execution should reach this only when removing an item from a completely full map
    return true;
}

bool std_hash_map_remove_hash ( std_hash_map_t* map, uint64_t hash ) {
    // Load
    size_t      mask = map->mask;
    uint64_t*   hashes = map->hashes;
    //uint64_t*   payloads = map->payloads;

    // Compute idx
    size_t      idx = hash & mask;

    // Linear probe for the right hash
    for ( size_t i = 0; i <= mask; ++i ) {
        uint64_t idx_hash = hashes[idx];

        if ( idx_hash == hash ) {
            //goto remove;
            return std_hash_map_remove_at_idx ( map, idx );
        } else if ( idx_hash == UINT64_MAX ) {
            return false;
        } else {
            idx = ( idx + 1 ) & mask;
        }
    }

    return false;

#if 0
remove: {
        // On remove we try to move up all items that follow the removed one until an empty slot
        // is encountered. The rule that all elements can be accessed by hash lookup + linear probing
        // must remain valid.
        size_t next_idx = idx;

        // This is just to ensure that at most we go through all elements in the map once.
        for ( size_t i = 0; i < mask + 1; ++i ) {
            next_idx = ( next_idx + 1 ) & mask;
            uint64_t next_hash = hashes[next_idx];

            // If next element is invalid, no more moving necessary. Can return.
            if ( next_hash == UINT64_MAX ) {
                hashes[idx] = UINT64_MAX;
                --map->count;
                return true;
            }

            // Check if next wants to move up.
            uint64_t next_ideal_idx = next_hash & mask;
            uint64_t after_swap_cost = std_ring_distance_u64 ( next_ideal_idx, idx, mask + 1 );
            uint64_t curr_cost = std_ring_distance_u64 ( next_ideal_idx, next_idx, mask + 1 );

            if ( after_swap_cost < curr_cost ) {
                hashes[idx] = next_hash;
                payloads[idx] = payloads[next_idx];
                // After swap, cell idx is now valid and cell next_idx is invalid, and the remove algorithm continues on next_idx.
                i += std_ring_distance_u64 ( idx, next_idx, mask + 1 );
                idx = next_idx;
            }
        }
    }

    // Execution should reach this only when removing an item from a completely full map
    return true;
#endif    
}

bool std_hash_map_remove_payload ( std_hash_map_t* map, uint64_t* payload ) {
    size_t idx = payload - map->payloads;
    return std_hash_map_remove_at_idx ( map, idx );
}

// ------

std_hash_set_t std_hash_set ( uint64_t* hashes, size_t capacity ) {
    std_hash_set_t set;
    set.hashes = hashes;
    set.count = 0;
    set.mask = capacity - 1;

    for ( size_t i = 0; i < capacity; ++i ) {
        set.hashes[i] = UINT64_MAX;
    }

    return set;
}

bool std_hash_set_insert ( std_hash_set_t* set, uint64_t hash ) {
    // Load
    size_t      mask = set->mask;
    uint64_t*   hashes = set->hashes;

    // Compute idx
    size_t      idx = hash & mask;

    // Redirect the hash value used to indicate empty to some other value slot
    if ( hash == UINT64_MAX ) {
        ++hash;
    }

    // Try insert until success (linear probing)
    for ( size_t i = 0; i < mask + 1; ++i ) {
        if ( hashes[idx] == UINT64_MAX ) {
            hashes[idx] = hash;
            ++set->count;
            return true;
        } else if ( hashes[idx] == hash ) {
            return false;
        }

        idx = ( idx + 1 ) & mask;
    }

    std_log_error_m ( "hash set is full!" );
    return false;
}

bool std_hash_set_remove ( std_hash_set_t* set, uint64_t hash ) {
    // Load
    size_t      mask = set->mask;
    uint64_t*   hashes = set->hashes;

    // Compute idx
    size_t      idx = hash & mask;

    // Linear probe for the right hash
    for ( size_t i = 0; i <= mask; ++i ) {
        uint64_t idx_hash = hashes[idx];

        if ( idx_hash == hash ) {
            goto remove;
        } else if ( idx_hash == UINT64_MAX ) {
            return false;
        } else {
            idx = ( idx + 1 ) & mask;
        }
    }

    return false;

remove: {
        // On remove we try to move up all items that follow the removed one until an empty slot
        // is encountered. The rule that all elements can be accessed by hash lookup + linear probing
        // must remain valid.
        size_t next_idx = idx;

        // This is just to ensure that at most we go through all elements in the map once.
        for ( size_t i = 0; i < mask + 1; ++i ) {
            next_idx = ( next_idx + 1 ) & mask;
            uint64_t next_hash = hashes[next_idx];

            // If next element is invalid, no more moving necessary. Can return.
            if ( next_hash == UINT64_MAX ) {
                hashes[idx] = UINT64_MAX;
                --set->count;
                return true;
            }

            // Check if next wants to move up.
            uint64_t next_ideal_idx = next_hash & mask;
            uint64_t after_swap_cost = std_ring_distance_u64 ( next_ideal_idx, idx, mask + 1 );
            uint64_t curr_cost = std_ring_distance_u64 ( next_ideal_idx, next_idx, mask + 1 );

            if ( after_swap_cost < curr_cost ) {
                hashes[idx] = next_hash;
                // After swap, cell idx is now valid and cell next_idx is invalid, and the remove algorithm continues on next_idx.
                i += std_ring_distance_u64 ( idx, next_idx, mask + 1 );
                idx = next_idx;
            }
        }
    }

    // Execution should reach this only when removing an item from a completely full map
    return true;
}

bool std_hash_set_lookup ( std_hash_set_t* set, uint64_t hash ) {
    // Load
    size_t          mask = set->mask;
    const uint64_t* hashes = set->hashes;

    // Compute idx
    size_t          idx = hash & mask;
    uint64_t        set_hash = hashes[idx];

    // Try compare until success or empty slot (linear probing)
    for ( size_t i = 0; i < mask + 1; ++i ) {
        if ( set_hash == UINT64_MAX ) {
            // Empty
            return NULL;
        }

        if ( hash == set_hash ) {
            // Success
            return true;
        }

        idx = ( idx + 1 ) & mask;
        set_hash = hashes[idx];
    }

    return false;
}

void std_hash_set_clear ( std_hash_set_t* set ) {
    set->count = 0;
    for ( size_t i = 0; i < set->mask + 1; ++i ) {
        set->hashes[i] = UINT64_MAX;
    }
}
