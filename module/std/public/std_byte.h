#pragma once

#include <std_platform.h>
#include <std_compiler.h>

void        std_mem_copy ( void* dest, const void* source, size_t size );
bool        std_mem_cmp  ( const void* a, const void* b, size_t size );
void        std_mem_set  ( void* dest, size_t size, char value );
void        std_mem_zero ( void* dest, size_t size );
bool        std_mem_test ( const void* base, size_t size, char value );
void        std_mem_move ( void* dest, void* source, size_t size );

#define     std_mem_zero_m( item ) std_mem_zero ( (item), sizeof ( *(item) ) )
#define     std_mem_zero_array_m( item, count ) std_mem_zero ( (item), sizeof ( *(item) ) * (count) )
#define     std_mem_zero_static_array_m( array ) std_mem_zero ( array, sizeof ( array ) )
#define     std_mem_copy_m( dest, source ) std_mem_copy( (dest), (source), sizeof ( std_typeof_m ( *(source) ) ) )
#define     std_mem_copy_array_m( dest, source, count ) std_mem_copy( (dest), (source), sizeof ( std_typeof_m ( *(source) ) ) * (count) )
#define     std_mem_copy_static_array_m( dest, source ) std_mem_copy( (dest), (source), sizeof ( source ) )
#define     std_mem_cmp_array_m(a, b, count) std_mem_cmp( (a), (b), sizeof ( std_typeof_m ( *(a) ) ) * (count) )
#define     std_mem_set_m( dest, value ) std_mem_set_m ( dest, sizeof ( *dest ), value )
#define     std_mem_set_static_array_m( array, value ) std_mem_set ( array, sizeof ( array ), value )

// Bit indexing starts from 0.
// ls/ms suffixed routines operate on count bits starting from ls/ms
// seq suffixed routines operate on a segment of bits specified by first bit index starting from ls and bit count
#define     std_bit_set_32_m( u32, bit ) ( (u32) | ( 1u << (bit) ) )
#define     std_bit_set_64_m( u64, bit ) ( (u64) | ( 1ull << (bit) ) )
#define     std_bit_set_ls_32_m( u32, bit_count ) ( (u32) | ~( ~0u << (bit_count) ) )
#define     std_bit_set_ls_64_m( u64, bit_count ) ( (u64) | ~( ~0ull << (bit_count) ) )
#define     std_bit_set_ms_32_m( u32, bit_count ) ( (u32) | ~( ~0u >> (bit_count) ) )
#define     std_bit_set_ms_64_m( u64, bit_count ) ( (u64) | ~( ~0ull >> (bit_count) ) )
#define     std_bit_set_seq_64_m( u64, first_bit, bit_count ) ( u64 | ( ( ~ ( ~0ull << (bit_count) ) ) << (first_bit) ) )

#define     std_bit_clear_32_m( u32, bit ) ( (u32) & ~ ( 1u << (bit) ) )
#define     std_bit_clear_64_m( u64, bit ) ( (u64) & ~ ( 1ull << (bit) ) )
#define     std_bit_clear_ls_32_m( u32, bit_count ) ( (u32) & ( ~0u << (bit_count) ) )
#define     std_bit_clear_ls_64_m( u64, bit_count ) ( (u64) & ( ~0ull << (bit_count) ) )
#define     std_bit_clear_ms_32_m( u32, bit_count ) ( (u32) & ( ~0u >> (bit_count) ) )
#define     std_bit_clear_ms_64_m( u64, bit_count ) ( (u64) & ( ~0ull >> (bit_count) ) )
#define     std_bit_clear_seq_64_m( u64, first_bit, bit_count ) ( (u64) & ( ( ( ~0ull << (bit_count) ) << (first_bit) ) | ( ~ ( ~0ull << (first_bit) ) ) ) )

#define     std_bit_toggle_32_m( u32, bit ) ( (u32) ^ 1u << (bit) )
#define     std_bit_toggle_64_m( u64, bit ) ( (u64) ^ 1ull << (bit) )

#define     std_bit_test_32_m( u32, bit ) ( (u32) & ( 1u << (bit) ) )
#define     std_bit_test_64_m( u64, bit ) ( (u64) & ( 1ull << (bit) ) )

#define     std_bit_read_32_m( u32, bit ) ( (u32) & ( 1u << (bit) ) ) == 1u << (bit)
#define     std_bit_read_64_m( u64, bit ) ( (u64) & ( 1ull << (bit) ) ) == 1ull << (bit)
#define     std_bit_read_ls_32_m( u32, bit_count ) ( (u32) & ( ~ ( ~0u << (bit_count) ) ) )
#define     std_bit_read_ls_64_m( u64, bit_count ) ( (u64) & ( ~ ( ~0ull << (bit_count) ) ) )
#define     std_bit_read_ms_32_m( u32, bit_count ) ( (u32) >> ( 32 - (bit_count) ) )
#define     std_bit_read_ms_64_m( u64, bit_count ) ( ( uint64_t ) (u64) >> ( 64 - (bit_count) ) )
#define     std_bit_read_seq_32_m( u32, first_bit, bit_count ) ( std_bit_read_ls_32_m ( (u32) >> (first_bit), bit_count ) )
#define     std_bit_read_seq_64_m( u64, first_bit, bit_count ) ( std_bit_read_ls_64_m ( ( uint64_t ) (u64) >> (first_bit), bit_count ) )

#define     std_bit_mask_ls_64_m( bit_count ) ( ( 1ull << bit_count ) - 1 )
#define     std_bit_mask_ms_64_m( bit_count ) ( ~ std_bit_mask_ls_64_m ( bit_count ) )

#define     std_bit_write_32_m( u32, bit, value ) ( ( (u32) & ~( 1u << (bit) ) ) | ( -(value) & ( 1u << (bit) ) ) )
#define     std_bit_write_64_m( u64, bit, value ) ( ( ( ( uint64_t ) (u64) ) & ~( 1ull << (bit) ) ) | ( -(value) & ( 1ull << (bit) ) ) )
#define     std_bit_write_ms_64_m( u64, bit_count, value ) ( ( ( uint64_t ) (value) << ( 64 - (bit_count) ) ) | ( (u64) & std_bit_mask_ls_64_m ( 64 - (bit_count) ) ) )

// Stores the position of the first bit found to be 1
// The lookup is performed in the required direction (default is lsb->msb)
// The index position stored is always an offset relative to the lsb/msb (starts from zero)
// Caller must ensure that the value passed is not 0, if that's the case the return value is undefined.
uint32_t    std_bit_scan_32         ( uint32_t u32 );
uint32_t    std_bit_scan_64         ( uint64_t u64 );
uint32_t    std_bit_scan_rev_32     ( uint32_t u32 );
uint32_t    std_bit_scan_rev_64     ( uint64_t u64 );
uint32_t    std_bit_scan_seq_64     ( uint64_t u64, size_t length );
uint32_t    std_bit_scan_seq_rev_64 ( uint64_t u64, size_t length );

size_t      std_bit_count_32 ( uint32_t value );
size_t      std_bit_count_64 ( uint64_t value );

size_t      std_align     ( size_t value, size_t align );
char*       std_align_ptr ( void* value, size_t align );
uint32_t    std_align_u32 ( uint32_t value, uint32_t align );
uint64_t    std_align_u64 ( uint64_t value, uint64_t align );

#define     std_align_ptr_m( ptr, type ) ( type* ) std_align_ptr ( ptr, std_alignof_m ( type ) )

bool        std_align_test     ( size_t value, size_t align );
bool        std_align_test_ptr ( void* value, size_t align );
bool        std_align_test_u32 ( uint32_t value, uint32_t align );
bool        std_align_test_u64 ( uint64_t value, uint64_t align );

size_t      std_align_size ( size_t value, size_t align );
size_t      std_align_size_ptr ( void* value, size_t align );

bool        std_pow2_test     ( size_t value );
bool        std_pow2_test_u32 ( uint32_t value );
bool        std_pow2_test_u64 ( uint64_t value );

size_t      std_pow2_round_up     ( size_t value );
uint32_t    std_pow2_round_up_u32 ( uint32_t value );
uint64_t    std_pow2_round_up_u64 ( uint64_t value );

// https://stackoverflow.com/questions/22925016/rounding-up-to-powers-of-2-with-preprocessor-constants
// This is limited to 32bit values
#define std_pow2_round_up_32_m(v) (1 + \
(((((((((v) - 1) | (((v) - 1) >> 0x10) | \
      (((v) - 1) | (((v) - 1) >> 0x10) >> 0x08)) | \
     ((((v) - 1) | (((v) - 1) >> 0x10) | \
      (((v) - 1) | (((v) - 1) >> 0x10) >> 0x08)) >> 0x04))) | \
   ((((((v) - 1) | (((v) - 1) >> 0x10) | \
      (((v) - 1) | (((v) - 1) >> 0x10) >> 0x08)) | \
     ((((v) - 1) | (((v) - 1) >> 0x10) | \
      (((v) - 1) | (((v) - 1) >> 0x10) >> 0x08)) >> 0x04))) >> 0x02))) | \
 ((((((((v) - 1) | (((v) - 1) >> 0x10) | \
      (((v) - 1) | (((v) - 1) >> 0x10) >> 0x08)) | \
     ((((v) - 1) | (((v) - 1) >> 0x10) | \
      (((v) - 1) | (((v) - 1) >> 0x10) >> 0x08)) >> 0x04))) | \
   ((((((v) - 1) | (((v) - 1) >> 0x10) | \
      (((v) - 1) | (((v) - 1) >> 0x10) >> 0x08)) | \
     ((((v) - 1) | (((v) - 1) >> 0x10) | \
      (((v) - 1) | (((v) - 1) >> 0x10) >> 0x08)) >> 0x04))) >> 0x02))) >> 0x01))))

size_t      std_pow2_round_down     ( size_t value );
uint32_t    std_pow2_round_down_u32 ( uint32_t value );
uint64_t    std_pow2_round_down_u64 ( uint64_t value );

// todo rename from ceil to roundup
size_t      std_div_ceil     ( size_t dividend, size_t divisor );
uint32_t    std_div_ceil_u32 ( uint32_t dividend, uint32_t divisor );
uint64_t    std_div_ceil_u64 ( uint64_t dividend, uint64_t divisor );

uint64_t    std_add_saturate_u64 ( size_t a, size_t b );
uint64_t    std_sub_saturate_u64 ( size_t a, size_t b );

size_t      std_min     ( size_t a, size_t b );
int32_t     std_min_i32 ( int32_t a, int32_t b );
int64_t     std_min_i64 ( int64_t a, int64_t b );
uint32_t    std_min_u32 ( uint32_t a, uint32_t b );
uint64_t    std_min_u64 ( uint64_t a, uint64_t b );
float       std_min_f32 ( float a, float b );
double      std_min_f64 ( double a, double b );

size_t      std_max     ( size_t a, size_t b );
int32_t     std_max_i32 ( int32_t a, int32_t b );
int64_t     std_max_i64 ( int64_t a, int64_t b );
uint32_t    std_max_u32 ( uint32_t a, uint32_t b );
uint64_t    std_max_u64 ( uint64_t a, uint64_t b );
float       std_max_f32 ( float a, float b );
double      std_max_f64 ( double a, double b );

void        std_u64_to_2_u32 ( uint32_t* high, uint32_t* low, uint64_t u64 );
uint64_t    std_2_u32_to_u64 ( uint32_t high, uint32_t low );

uint64_t    std_ring_distance_u64 ( uint64_t from, uint64_t to, uint64_t ring_size );

#define std_bitset_u64_count_m( capacity ) std_div_ceil_m ( capacity, 64 )
// call std_mem_set to initialize the bitset to the desired initial value
void std_bitset_set ( uint64_t* bitset, size_t idx );
bool std_bitset_test ( const uint64_t* bitset, size_t idx );
void std_bitset_clear ( uint64_t* bitset, size_t idx );
void std_bitset_write ( uint64_t* bitset, size_t idx, uint32_t value );
// scans for the first bit set to 1. the bit at location starting_bit_idx is included in the scan
// u64_blocks_count is to always be specified relative to the very beginning of the bitset, not from the given starting bit
// returns false if no bit set to 1 was found
bool std_bitset_scan ( uint64_t* out_idx, const uint64_t* bitset, size_t starting_bit_idx, size_t total_u64_count );
bool std_bitset_scan_rev ( uint64_t* out_idx, const uint64_t* bitset, size_t starting_bit_idx );
// shift the entire bitset
void std_bitset_shift_left ( uint64_t* bitset, size_t starting_bit_idx, size_t shift_count, size_t total_u64_count );
void std_bitset_shift_right ( uint64_t* bitset, size_t starting_bit_idx, size_t shift_count, size_t total_u64_count );

bool std_bitset_set_atomic ( uint64_t* bitset, size_t idx );
bool std_bitset_clear_atomic ( uint64_t* bitset, size_t idx );
