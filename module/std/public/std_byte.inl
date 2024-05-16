#pragma once

#include <std_log.h>
#include <std_platform.h>

// http://graphics.stanford.edu/~seander/bithacks.html

// ------------------------------------------------------------------------------------------------------
// Mem
// ------------------------------------------------------------------------------------------------------
inline void std_mem_copy ( void* dest, const void* source, size_t size ) {
    memcpy ( dest, source, size );
}
inline void std_mem_set ( void* dest, size_t size, char value ) {
    memset ( dest, value, size );
}
inline bool std_mem_cmp ( const void* a, const void* b, size_t size ) {
    return memcmp ( a, b, size ) == 0;
}
inline void std_mem_zero ( void* dest, size_t size ) {
    memset ( dest, 0, size );
}

inline bool std_mem_test ( const void* base, size_t size, char value ) {
    // TODO faster implementation
    char* b = ( char* ) base;

    for ( size_t i = 0; i < size; ++i ) {
        if ( b[i] != value ) {
            return false;
        }
    }

    return true;
}

inline void std_mem_move ( void* dest, void* source, size_t size ) {
    memmove ( dest, source, size );
}

// ------------------------------------------------------------------------------------------------------
// Bit set
// ------------------------------------------------------------------------------------------------------
inline void std_bit_set_32 ( uint32_t* value, size_t bit ) {
    *value |= 1u << bit;
}

inline void std_bit_set_64 ( uint64_t* value, uint64_t bit ) {
    *value |= 1ull << bit;
}

inline void std_bit_toggle_32 ( uint32_t* value, size_t bit ) {
    *value ^= 1u << bit;
}

inline void std_bit_toggle_64 ( uint64_t* value, size_t bit ) {
    *value ^= 1ull << bit;
}

inline void std_bit_set_ls_32  ( uint32_t* value, size_t n ) {
    *value |= ( uint32_t ) ~ ( ~0u << n );
}

inline void std_bit_set_ls_64 ( uint64_t* value, size_t n ) {
    *value |= ( uint64_t ) ~ ( ~0ull << n );
}

inline void std_bit_set_ms_32 ( uint32_t* value, size_t n ) {
    *value |= ( uint32_t ) ~ ( ~0u >> n );
}

inline void std_bit_set_ms_64 ( uint64_t* value, size_t n ) {
    *value |= ( uint64_t ) ~ ( ~0ull >> n );
}

inline void std_bit_set_seq_64 ( uint64_t* value, size_t ls_offset, size_t n ) {
    *value |= ( ~ ( ~0ull << n ) ) << ls_offset;
}

inline void std_bit_set_all_32 ( uint32_t* value ) {
    *value = UINT32_MAX;
}

inline void std_bit_set_all_64 ( uint64_t* value ) {
    *value = UINT64_MAX;
}

inline void std_bit_clear_32 ( uint32_t* value, size_t bit ) {
    *value &= ~ ( 1u << bit );
}

inline void std_bit_clear_64 ( uint64_t* value, size_t bit ) {
    *value &= ~ ( 1ull << bit );
}

inline void std_bit_clear_ls_64 ( uint64_t* value, size_t n ) {
    *value &= ( ~0ull << n );
}

inline void std_bit_clear_ms_64 ( uint64_t* value, size_t n ) {
    *value &= ( ~0ull >> n );
}

inline void std_bit_clear_ls_32 ( uint32_t* value, size_t n ) {
    *value &= ( ~0u << n );
}

inline void std_bit_clear_ms_32 ( uint32_t* value, size_t n ) {
    *value &= ( ~0u >> n );
}

inline void std_bit_clear_seq_64 ( uint64_t* value, size_t ls_offset, size_t n ) {
    *value &= ( ( ( ~0ull << n ) << ls_offset ) | ( ~ ( ~0ull << ls_offset ) ) );
}

inline size_t std_bit_count_32 ( uint32_t value ) {
#if defined(std_platform_win32_m)
    return __popcnt ( value );
#elif defined(std_platform_linux_m)
    return __builtin_popcount ( value );
#endif
}

inline size_t std_bit_count_64 ( uint64_t value ) {
#if defined(std_platform_win32_m)
    return __popcnt64 ( value );
#elif defined(std_platform_linux_m)
    return __builtin_popcountll ( value );
#endif
}

inline uint64_t std_bit_read_64 ( uint64_t value, size_t bit ) {
    return ( value & ( 1 << bit ) ) == 1 << bit;
}

inline uint64_t std_bit_read_ls_64 ( uint64_t value, size_t n ) {
    uint64_t mask = 0;
    std_bit_set_ls_64 ( &mask, n );
    return value & mask;
}

inline uint64_t std_bit_read_ms_64 ( uint64_t value, size_t n ) {
    uint64_t mask = 0;
    std_bit_set_ms_64 ( &mask, n );
    return ( value & mask ) >> ( 64 - n );
}

inline uint64_t std_bit_read_seq_64 ( uint64_t value, size_t ls_offset, size_t n ) {
    value = value >> ls_offset;
    return std_bit_read_ls_64 ( value, n );
}

inline uint32_t std_bit_read_32 ( uint32_t value, size_t bit ) {
    return ( value & ( 1 << bit ) ) == 1 << bit;
}

inline uint32_t std_bit_read_ls_32 ( uint32_t value, size_t n ) {
    uint32_t mask = 0;
    std_bit_set_ls_32 ( &mask, n );
    return value & mask;
}

inline uint32_t std_bit_read_ms_32 ( uint32_t value, size_t n ) {
    return value >> ( 32 - n );
}

inline uint32_t std_bit_read_seq_32 ( uint32_t value, size_t ls_offset, size_t n ) {
    value = value >> ls_offset;
    return std_bit_read_ls_32 ( value, n );
}

inline bool std_bit_test_64 ( uint64_t value, uint32_t bit ) {
    return value & ( 1 << bit );
}

inline void std_bit_write_32 ( uint32_t* value, size_t bit, uint32_t x ) {
    std_bit_clear_32 ( value, bit );
    *value |= ( x << bit );
}

inline void std_bit_write_64 ( uint64_t* value, size_t bit, uint64_t x ) {
    std_bit_clear_64 ( value, bit );
    *value |= ( x << bit );
}

inline void std_bit_write_ls_64 ( uint64_t* value, uint64_t write, size_t n ) {
    std_bit_clear_ls_64 ( value, n );
    uint64_t mask = 0;
    std_bit_set_ls_64 ( &mask, n );
    *value |= write & mask;
}

inline void std_bit_write_ms_64 ( uint64_t* value, uint64_t write, size_t n ) {
    write = write << n;
    std_bit_clear_ms_64 ( value, n );
    *value |= write;
}

inline void std_bit_write_repeat_ls_64 ( uint64_t* value, uint64_t write, size_t n ) {
    uint64_t bitset = write - 1;    // all 1s if value==0, all 0s if value==1
    std_bit_write_ls_64 ( value, bitset, n );
}

inline void std_bit_write_repeat_ms_64 ( uint64_t* value, uint64_t write, size_t n ) {
    uint64_t bitset = write - 1;    // all 1s if value==0, all 0s if value==1
    std_bit_write_ms_64 ( value, bitset, n );
}

// ------------------------------------------------------------------------------------------------------
// Bit scan
// ------------------------------------------------------------------------------------------------------
inline uint32_t std_bit_scan_32 ( uint32_t value ) {
#ifdef std_platform_win32_m
    uint32_t index;
    _BitScanForward ( ( DWORD* ) &index, value );
    return index;
#elif defined(std_platform_linux_m)
    return __builtin_ctz ( value );
#endif
}

inline uint32_t std_bit_scan_64 ( uint64_t value ) {
#ifdef std_platform_win32_m
    uint32_t index;
    _BitScanForward64 ( ( DWORD* ) &index, value );
    return index;
#elif defined(std_platform_linux_m)
    return __builtin_ctzll ( value );
#endif
}

inline uint32_t std_bit_scan_rev_32 ( uint32_t value ) {
#ifdef std_platform_win32_m
    uint32_t index;
    _BitScanReverse ( ( DWORD* ) &index, value );
    return 31 - index;
#elif defined(std_platform_linux_m)
    return __builtin_clz ( value );
#endif
}

inline uint32_t std_bit_scan_rev_64 ( uint64_t value ) {
#ifdef std_platform_win32_m
    uint32_t index;
    _BitScanReverse64 ( ( DWORD* ) &index, value );
    return 63 - index;
#elif defined(std_platform_linux_m)
    return __builtin_clzll ( value );
#endif
}

inline uint32_t std_bit_scan_seq_64 ( uint64_t value, size_t length ) {
    while ( length > 1 ) {
        uint64_t shift = length >> 1;
        value &= value >> shift;
        length -= shift;
    }

    return std_bit_scan_64 ( value );
}

inline uint32_t std_bit_scan_seq_rev_64 ( uint64_t value, size_t length ) {
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
inline bool std_pow2_test ( size_t value ) {
    return ( value & ( value - 1 ) ) == 0;
}

inline bool std_pow2_test_u32 ( uint32_t value ) {
    return ( value & ( value - 1 ) ) == 0;
}

inline bool std_pow2_test_u64 ( uint64_t value ) {
    return ( value & ( value - 1 ) ) == 0;
}

inline bool std_align_test ( size_t value, size_t align ) {
    return ( value & ( align - 1 ) ) == 0;
}

inline bool std_align_test_ptr ( void* value, size_t align ) {
    return ( ( size_t ) value & ( align - 1 ) ) == 0;
}

inline bool std_align_test_u32 ( uint32_t value, uint32_t align ) {
    return ( value & ( align - 1 ) ) == 0;
}

inline bool std_align_test_u64 ( uint64_t value, uint64_t align ) {
    return ( value & ( align - 1 ) ) == 0;
}

inline size_t std_align_size ( size_t value, size_t align ) {
    size_t mask = align - 1;
    return ( align - ( value & mask ) ) & mask;
}

inline size_t std_align_size_ptr ( void* value, size_t align ) {
    size_t mask = align - 1;
    return ( align - ( ( size_t ) value & mask ) ) & mask;
}

inline char* std_align_ptr ( void* value, size_t align ) {
    std_assert_m ( align != 0 );
    std_assert_m ( std_pow2_test ( align ) );
    size_t mask = align - 1;
    return ( char* ) ( ( ( size_t ) value + mask ) & ~mask );
}

inline size_t std_align ( size_t value, size_t align ) {
    std_assert_m ( align != 0 );
    std_assert_m ( std_pow2_test ( align ) );
    size_t mask = align - 1;
    return ( ( value + mask ) & ~mask );
}

inline uint32_t std_align_u32 ( uint32_t value, uint32_t align ) {
    std_assert_m ( align != 0 );
    std_assert_m ( std_pow2_test_u32 ( align ) );
    uint32_t mask = align - 1;
    return ( ( value + mask ) & ~mask );
}

inline uint64_t std_align_u64 ( uint64_t value, uint64_t align ) {
    std_assert_m ( align != 0 );
    std_assert_m ( std_pow2_test_u64 ( align ) );
    uint64_t mask = align - 1;
    return ( ( value + mask ) & ~mask );
}

inline size_t std_pow2_round_up ( size_t value ) {
#if std_pointer_size_m == 8
    return std_pow2_round_up_u64 ( value );
#elif std_pointer_size_m == 4
    return std_pow2_round_up_u32 ( value );
#else
#error
#endif
}

inline uint32_t std_pow2_round_up_u32 ( uint32_t v ) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

inline uint64_t std_pow2_round_up_u64 ( uint64_t v ) {
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

inline size_t std_pow2_round_down ( size_t value ) {
#if std_pointer_size_m == 8
    return std_pow2_round_down_u64 ( value );
#elif std_pointer_size_m == 4
    return std_pow2_round_down_u32 ( value );
#else
#error
#endif
}

inline uint32_t std_pow2_round_down_u32 ( uint32_t v ) {
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v - ( v >> 1 );
}

inline uint64_t std_pow2_round_down_u64 ( uint64_t v ) {
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    return v - ( v >> 1 );
}

inline size_t std_div_ceil ( size_t dividend, size_t divisor ) {
    return ( dividend + divisor - 1 ) / divisor;
}

inline uint32_t std_div_ceil_u32 ( uint32_t dividend, uint32_t divisor ) {
    return ( dividend + divisor - 1 ) / divisor;
}

inline uint64_t std_div_ceil_u64 ( uint64_t dividend, uint64_t divisor ) {
    return ( dividend + divisor - 1 ) / divisor;
}

// https://stackoverflow.com/questions/33481295/subtract-add-value-without-overflow-or-underflow
inline uint64_t std_add_saturate_u64 ( size_t a, size_t b ) {
    uint64_t res = a + b;
    res |= - ( res < a );
    return res;
}
inline uint64_t std_sub_saturate_u64 ( size_t a, size_t b ) {
    uint64_t res = a - b;
    res &= - ( res <= a );
    return res;
}

inline size_t std_min ( size_t a, size_t b ) {
    return a < b ? a : b;
}

inline int32_t std_min_i32 ( int32_t a, int32_t b ) {
    return a < b ? a : b;
}

inline int64_t std_min_i64 ( int64_t a, int64_t b ) {
    return a < b ? a : b;
}

inline uint32_t std_min_u32 ( uint32_t a, uint32_t b ) {
    return a < b ? a : b;
}

inline uint32_t std_min_u64 ( uint32_t a, uint32_t b ) {
    return a < b ? a : b;
}

inline float std_min_f32 ( float a, float b ) {
    return a < b ? a : b;
}

inline double std_min_f64 ( double a, double b ) {
    return a < b ? a : b;
}

inline size_t std_max ( size_t a, size_t b ) {
    return a > b ? a : b;
}

inline int32_t std_max_i32 ( int32_t a, int32_t b ) {
    return a > b ? a : b;
}

inline int64_t std_max_i64 ( int64_t a, int64_t b ) {
    return a > b ? a : b;
}

inline uint32_t std_max_u32 ( uint32_t a, uint32_t b ) {
    return a > b ? a : b;
}

inline uint64_t std_max_u64 ( uint64_t a, uint64_t b ) {
    return a > b ? a : b;
}

inline float std_max_f32 ( float a, float b ) {
    return a > b ? a : b;
}

inline double std_max_f64 ( double a, double b ) {
    return a > b ? a : b;
}

inline void std_u64_to_2_u32 ( uint32_t* high, uint32_t* low, uint64_t u64 ) {
    *high = ( uint32_t ) ( ( u64 & 0xFFFFFFFF00000000ULL ) >> 32 );
    *low = ( uint32_t ) ( u64 & 0xFFFFFFFFULL );
}

inline uint64_t std_2_u32_to_u64 ( uint32_t high, uint32_t low ) {
    return ( ( uint64_t ) high ) << 32 | low;
}

inline uint64_t std_ring_distance_u64 ( uint64_t a, uint64_t b, uint64_t ring_size ) {
    return a <= b ? b - a : ring_size - b + a;
}
