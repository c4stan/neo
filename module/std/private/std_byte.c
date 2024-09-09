#include <std_byte.h>

#include <std_platform.h>
#include <std_log.h>
#include <std_atomic.h>

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#if 1

// http://graphics.stanford.edu/~seander/bithacks.html

// ------------------------------------------------------------------------------------------------------
// Mem
// ------------------------------------------------------------------------------------------------------
void std_mem_copy ( void* dest, const void* source, size_t size ) {
    memcpy ( dest, source, size );
}
void std_mem_set ( void* dest, size_t size, char value ) {
    memset ( dest, value, size );
}
bool std_mem_cmp ( const void* a, const void* b, size_t size ) {
    return memcmp ( a, b, size ) == 0;
}
void std_mem_zero ( void* dest, size_t size ) {
    memset ( dest, 0, size );
}

bool std_mem_test ( const void* base, size_t size, char value ) {
    // TODO faster implementation
    char* b = ( char* ) base;

    for ( size_t i = 0; i < size; ++i ) {
        if ( b[i] != value ) {
            return false;
        }
    }

    return true;
}

void std_mem_move ( void* dest, void* source, size_t size ) {
    memmove ( dest, source, size );
}

// ------------------------------------------------------------------------------------------------------
// Bit set
// ------------------------------------------------------------------------------------------------------
void std_bit_set_32 ( uint32_t* value, size_t bit ) {
    *value |= 1u << bit;
}

void std_bit_set_64 ( uint64_t* value, uint64_t bit ) {
    *value |= 1ull << bit;
}

void std_bit_toggle_32 ( uint32_t* value, size_t bit ) {
    *value ^= 1u << bit;
}

void std_bit_toggle_64 ( uint64_t* value, size_t bit ) {
    *value ^= 1ull << bit;
}

void std_bit_set_ls_32  ( uint32_t* value, size_t n ) {
    *value |= ( uint32_t ) ~ ( ~0u << n );
}

void std_bit_set_ls_64 ( uint64_t* value, size_t n ) {
    *value |= ( uint64_t ) ~ ( ~0ull << n );
}

void std_bit_set_ms_32 ( uint32_t* value, size_t n ) {
    *value |= ( uint32_t ) ~ ( ~0u >> n );
}

void std_bit_set_ms_64 ( uint64_t* value, size_t n ) {
    *value |= ( uint64_t ) ~ ( ~0ull >> n );
}

void std_bit_set_seq_64 ( uint64_t* value, size_t ls_offset, size_t n ) {
    *value |= ( ~ ( ~0ull << n ) ) << ls_offset;
}

void std_bit_set_all_32 ( uint32_t* value ) {
    *value = UINT32_MAX;
}

void std_bit_set_all_64 ( uint64_t* value ) {
    *value = UINT64_MAX;
}

void std_bit_clear_32 ( uint32_t* value, size_t bit ) {
    *value &= ~ ( 1u << bit );
}

void std_bit_clear_64 ( uint64_t* value, size_t bit ) {
    *value &= ~ ( 1ull << bit );
}

void std_bit_clear_ls_64 ( uint64_t* value, size_t n ) {
    *value &= ( ~0ull << n );
}

void std_bit_clear_ms_64 ( uint64_t* value, size_t n ) {
    *value &= ( ~0ull >> n );
}

void std_bit_clear_ls_32 ( uint32_t* value, size_t n ) {
    *value &= ( ~0u << n );
}

void std_bit_clear_ms_32 ( uint32_t* value, size_t n ) {
    *value &= ( ~0u >> n );
}

void std_bit_clear_seq_64 ( uint64_t* value, size_t ls_offset, size_t n ) {
    *value &= ( ( ( ~0ull << n ) << ls_offset ) | ( ~ ( ~0ull << ls_offset ) ) );
}

size_t std_bit_count_32 ( uint32_t value ) {
#if defined(std_platform_win32_m)
    return __popcnt ( value );
#elif defined(std_platform_linux_m)
    return __builtin_popcount ( value );
#endif
}

size_t std_bit_count_64 ( uint64_t value ) {
#if defined(std_platform_win32_m)
    return __popcnt64 ( value );
#elif defined(std_platform_linux_m)
    return __builtin_popcountll ( value );
#endif
}

uint64_t std_bit_read_64 ( uint64_t value, size_t bit ) {
    return ( value & ( 1ull << bit ) ) == 1ull << bit;
}

uint64_t std_bit_read_ls_64 ( uint64_t value, size_t n ) {
    uint64_t mask = 0;
    std_bit_set_ls_64 ( &mask, n );
    return value & mask;
}

uint64_t std_bit_read_ms_64 ( uint64_t value, size_t n ) {
    uint64_t mask = 0;
    std_bit_set_ms_64 ( &mask, n );
    return ( value & mask ) >> ( 64 - n );
}

uint64_t std_bit_read_seq_64 ( uint64_t value, size_t ls_offset, size_t n ) {
    value = value >> ls_offset;
    return std_bit_read_ls_64 ( value, n );
}

uint32_t std_bit_read_32 ( uint32_t value, size_t bit ) {
    return ( value & ( 1 << bit ) ) == 1 << bit;
}

uint32_t std_bit_read_ls_32 ( uint32_t value, size_t n ) {
    uint32_t mask = 0;
    std_bit_set_ls_32 ( &mask, n );
    return value & mask;
}

uint32_t std_bit_read_ms_32 ( uint32_t value, size_t n ) {
    return value >> ( 32 - n );
}

uint32_t std_bit_read_seq_32 ( uint32_t value, size_t ls_offset, size_t n ) {
    value = value >> ls_offset;
    return std_bit_read_ls_32 ( value, n );
}

bool std_bit_test_64 ( uint64_t value, uint32_t bit ) {
    return value & ( 1ull << bit );
}

void std_bit_write_32 ( uint32_t* value, size_t bit, uint32_t x ) {
    std_bit_clear_32 ( value, bit );
    *value |= ( x << bit );
}

void std_bit_write_64 ( uint64_t* value, size_t bit, uint64_t x ) {
    std_bit_clear_64 ( value, bit );
    *value |= ( x << bit );
}

void std_bit_write_ls_64 ( uint64_t* value, uint64_t write, size_t n ) {
    std_bit_clear_ls_64 ( value, n );
    uint64_t mask = 0;
    std_bit_set_ls_64 ( &mask, n );
    *value |= write & mask;
}

void std_bit_write_ms_64 ( uint64_t* value, uint64_t write, size_t n ) {
    write = write << n;
    std_bit_clear_ms_64 ( value, n );
    *value |= write;
}

void std_bit_write_repeat_ls_64 ( uint64_t* value, uint64_t write, size_t n ) {
    uint64_t bitset = write - 1;    // all 1s if value==0, all 0s if value==1
    std_bit_write_ls_64 ( value, bitset, n );
}

void std_bit_write_repeat_ms_64 ( uint64_t* value, uint64_t write, size_t n ) {
    uint64_t bitset = write - 1;    // all 1s if value==0, all 0s if value==1
    std_bit_write_ms_64 ( value, bitset, n );
}

// ------------------------------------------------------------------------------------------------------
// Bit scan
// ------------------------------------------------------------------------------------------------------
uint32_t std_bit_scan_32 ( uint32_t value ) {
#ifdef std_platform_win32_m
    uint32_t index;
    _BitScanForward ( ( DWORD* ) &index, value );
    return index;
#elif defined(std_platform_linux_m)
    return __builtin_ctz ( value );
#endif
}

uint32_t std_bit_scan_64 ( uint64_t value ) {
#ifdef std_platform_win32_m
    uint32_t index;
    _BitScanForward64 ( ( DWORD* ) &index, value );
    return index;
#elif defined(std_platform_linux_m)
    return __builtin_ctzll ( value );
#endif
}

uint32_t std_bit_scan_rev_32 ( uint32_t value ) {
#ifdef std_platform_win32_m
    uint32_t index;
    _BitScanReverse ( ( DWORD* ) &index, value );
    return 31 - index;
#elif defined(std_platform_linux_m)
    return __builtin_clz ( value );
#endif
}

uint32_t std_bit_scan_rev_64 ( uint64_t value ) {
#ifdef std_platform_win32_m
    uint32_t index;
    _BitScanReverse64 ( ( DWORD* ) &index, value );
    return 63 - index;
#elif defined(std_platform_linux_m)
    return __builtin_clzll ( value );
#endif
}

uint32_t std_bit_scan_seq_64 ( uint64_t value, size_t length ) {
    while ( length > 1 ) {
        uint64_t shift = length >> 1;
        value &= value >> shift;
        length -= shift;
    }

    return std_bit_scan_64 ( value );
}

uint32_t std_bit_scan_seq_rev_64 ( uint64_t value, size_t length ) {
    while ( length > 1 ) {
        uint64_t shift = length >> 1;
        value &= value << shift;
        length -= shift;
    }

    return std_bit_scan_rev_64 ( value );
}

// ------------------------------------------------------------------------------------------------------
// Bitwise math operations
// ------------------------------------------------------------------------------------------------------
bool std_pow2_test ( size_t value ) {
    return ( value & ( value - 1 ) ) == 0;
}

bool std_pow2_test_u32 ( uint32_t value ) {
    return ( value & ( value - 1 ) ) == 0;
}

bool std_pow2_test_u64 ( uint64_t value ) {
    return ( value & ( value - 1 ) ) == 0;
}

bool std_align_test ( size_t value, size_t align ) {
    return ( value & ( align - 1 ) ) == 0;
}

bool std_align_test_ptr ( void* value, size_t align ) {
    return ( ( size_t ) value & ( align - 1 ) ) == 0;
}

bool std_align_test_u32 ( uint32_t value, uint32_t align ) {
    return ( value & ( align - 1 ) ) == 0;
}

bool std_align_test_u64 ( uint64_t value, uint64_t align ) {
    return ( value & ( align - 1 ) ) == 0;
}

size_t std_align_size ( size_t value, size_t align ) {
    size_t mask = align - 1;
    return ( align - ( value & mask ) ) & mask;
}

size_t std_align_size_ptr ( void* value, size_t align ) {
    size_t mask = align - 1;
    return ( align - ( ( size_t ) value & mask ) ) & mask;
}

char* std_align_ptr ( void* value, size_t align ) {
    std_assert_m ( align != 0 );
    std_assert_m ( std_pow2_test ( align ) );
    size_t mask = align - 1;
    return ( char* ) ( ( ( size_t ) value + mask ) & ~mask );
}

size_t std_align ( size_t value, size_t align ) {
    std_assert_m ( align != 0 );
    std_assert_m ( std_pow2_test ( align ) );
    size_t mask = align - 1;
    return ( ( value + mask ) & ~mask );
}

uint32_t std_align_u32 ( uint32_t value, uint32_t align ) {
    std_assert_m ( align != 0 );
    std_assert_m ( std_pow2_test_u32 ( align ) );
    uint32_t mask = align - 1;
    return ( ( value + mask ) & ~mask );
}

uint64_t std_align_u64 ( uint64_t value, uint64_t align ) {
    std_assert_m ( align != 0 );
    std_assert_m ( std_pow2_test_u64 ( align ) );
    uint64_t mask = align - 1;
    return ( ( value + mask ) & ~mask );
}

size_t std_pow2_round_up ( size_t value ) {
#if std_pointer_size_m == 8
    return std_pow2_round_up_u64 ( value );
#elif std_pointer_size_m == 4
    return std_pow2_round_up_u32 ( value );
#else
#error
#endif
}

uint32_t std_pow2_round_up_u32 ( uint32_t v ) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

uint64_t std_pow2_round_up_u64 ( uint64_t v ) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    v++;
    return v;
}

size_t std_pow2_round_down ( size_t value ) {
#if std_pointer_size_m == 8
    return std_pow2_round_down_u64 ( value );
#elif std_pointer_size_m == 4
    return std_pow2_round_down_u32 ( value );
#else
#error
#endif
}

uint32_t std_pow2_round_down_u32 ( uint32_t v ) {
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v - ( v >> 1 );
}

uint64_t std_pow2_round_down_u64 ( uint64_t v ) {
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    return v - ( v >> 1 );
}

size_t std_div_ceil ( size_t dividend, size_t divisor ) {
    return ( dividend + divisor - 1 ) / divisor;
}

uint32_t std_div_ceil_u32 ( uint32_t dividend, uint32_t divisor ) {
    return ( dividend + divisor - 1 ) / divisor;
}

uint64_t std_div_ceil_u64 ( uint64_t dividend, uint64_t divisor ) {
    return ( dividend + divisor - 1 ) / divisor;
}

// https://stackoverflow.com/questions/33481295/subtract-add-value-without-overflow-or-underflow
uint64_t std_add_saturate_u64 ( size_t a, size_t b ) {
    uint64_t res = a + b;
    res |= - ( res < a );
    return res;
}
uint64_t std_sub_saturate_u64 ( size_t a, size_t b ) {
    uint64_t res = a - b;
    res &= - ( res <= a );
    return res;
}

size_t std_min ( size_t a, size_t b ) {
    return a < b ? a : b;
}

int32_t std_min_i32 ( int32_t a, int32_t b ) {
    return a < b ? a : b;
}

int64_t std_min_i64 ( int64_t a, int64_t b ) {
    return a < b ? a : b;
}

uint32_t std_min_u32 ( uint32_t a, uint32_t b ) {
    return a < b ? a : b;
}

uint32_t std_min_u64 ( uint32_t a, uint32_t b ) {
    return a < b ? a : b;
}

float std_min_f32 ( float a, float b ) {
    return a < b ? a : b;
}

double std_min_f64 ( double a, double b ) {
    return a < b ? a : b;
}

size_t std_max ( size_t a, size_t b ) {
    return a > b ? a : b;
}

int32_t std_max_i32 ( int32_t a, int32_t b ) {
    return a > b ? a : b;
}

int64_t std_max_i64 ( int64_t a, int64_t b ) {
    return a > b ? a : b;
}

uint32_t std_max_u32 ( uint32_t a, uint32_t b ) {
    return a > b ? a : b;
}

uint64_t std_max_u64 ( uint64_t a, uint64_t b ) {
    return a > b ? a : b;
}

float std_max_f32 ( float a, float b ) {
    return a > b ? a : b;
}

double std_max_f64 ( double a, double b ) {
    return a > b ? a : b;
}

void std_u64_to_2_u32 ( uint32_t* high, uint32_t* low, uint64_t u64 ) {
    *high = ( uint32_t ) ( ( u64 & 0xFFFFFFFF00000000ULL ) >> 32 );
    *low = ( uint32_t ) ( u64 & 0xFFFFFFFFULL );
}

uint64_t std_2_u32_to_u64 ( uint32_t high, uint32_t low ) {
    return ( ( uint64_t ) high ) << 32 | low;
}

uint64_t std_ring_distance_u64 ( uint64_t a, uint64_t b, uint64_t ring_size ) {
    return a <= b ? b - a : ring_size - b + a;
}

#endif

// Bitset
// TODO use 32 bit blocks?
bool std_bitset_test ( const uint64_t* blocks, size_t idx ) {
    uint64_t block_idx = idx >> 6;
    uint64_t bit_idx = idx & 0x3f; // 63, 2^6 - 1
    uint64_t block = blocks[block_idx];
    bool test = block & ( 1ull << bit_idx );
    return test;
}

void std_bitset_set ( uint64_t* blocks, size_t idx ) {
    uint64_t block_idx = idx >> 6;
    uint64_t bit_idx = idx & 0x3f;
    uint64_t block = blocks[block_idx];
    block = block | ( 1ull << bit_idx );
    blocks[block_idx] = block;
}

bool std_bitset_set_atomic ( uint64_t* blocks, size_t idx ) {
    uint64_t block_idx = idx >> 6;
    uint64_t bit_idx = idx & 0x3f;
    uint64_t* block = &blocks[block_idx];
    uint64_t old_block = *block;
    uint64_t new_block = old_block | ( 1ull << bit_idx );
    return std_compare_and_swap_u64 ( block, &old_block, new_block );
}

void std_bitset_clear ( uint64_t* blocks, size_t idx ) {
    uint64_t block_idx = idx >> 6;
    uint64_t bit_idx = idx & 0x3f;
    uint64_t block = blocks[block_idx];
    uint64_t mask = ~ ( 1ull << bit_idx );
    block = block & mask;
    blocks[block_idx] = block;
}

bool std_bitset_clear_atomic ( uint64_t* blocks, size_t idx ) {
    uint64_t block_idx = idx >> 6;
    uint64_t bit_idx = idx & 0x3f;
    uint64_t* block = &blocks[block_idx];
    uint64_t old_block = *block;
    uint64_t mask = ~ ( 1ull << bit_idx );
    uint64_t new_block = old_block & mask;
    return std_compare_and_swap_u64 ( block, &old_block, new_block );
}

bool std_bitset_scan ( uint64_t* result_bit_idx, const uint64_t* blocks, size_t starting_bit_idx, size_t u64_blocks_count ) {
    uint64_t block_idx = starting_bit_idx >> 6;
    uint64_t bit_idx = starting_bit_idx & 0x3f;

    uint64_t block = blocks[block_idx];
    std_bit_clear_ls_64 ( &block, bit_idx );

    for ( ;; ) {

        if ( block ) {
            uint32_t result_idx = std_bit_scan_64 ( block );
            *result_bit_idx = ( block_idx << 6 ) + result_idx;
            return true;
        }

        ++block_idx;

        if ( block_idx == u64_blocks_count ) {
            break;
        }

        block = blocks[block_idx];
    }

    return false;
}

bool std_bitset_scan_rev ( uint64_t* result_bit_idx, const uint64_t* blocks, size_t starting_bit_idx ) {
    uint64_t block_idx = starting_bit_idx >> 6;
    uint64_t bit_idx = starting_bit_idx & 0x3f;

    uint64_t block = blocks[block_idx];
    std_bit_clear_ms_64 ( &block, 63 - bit_idx );

    for ( ;; ) {

        if ( block ) {
            uint32_t result_idx = std_bit_scan_64 ( block );
            *result_bit_idx = ( block_idx << 6 ) + result_idx;
            return true;
        }

        --block_idx;

        if ( block_idx == UINT64_MAX ) {
            break;
        }

        block = blocks[block_idx];
    }

    return false;
}
