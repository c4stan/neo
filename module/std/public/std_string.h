#pragma once

#include <std_platform.h>

// String length is to be intended as the result of strlen(), even on utf8 strings. It is the byte size of the string without the terminator.
// To know how many actual single readable characters are encoded in a string, use str_count.
// Capacity(cap) parameters are to be specified in raw byte size, including the terminator character.

bool std_utf8_is_single_byte    ( char c );
bool std_utf8_is_double_byte    ( char c );
bool std_utf8_is_triple_byte    ( char c );
bool std_utf8_is_quadruple_byte ( char c );
bool std_utf8_is_first_byte     ( char c );
size_t std_utf8_char_size         ( char first_byte );
size_t std_utf8_char_size_reverse ( const char* last_byte, const char* str_base );  // back_len is distance from string base to last_byte

size_t std_str_count_ascii ( const char* str );
size_t std_str_count_utf8  ( const char* str );

size_t std_str_len ( const char* str );
// returns the length of source. can be used to test for success by comparing the result with dest_cap.
size_t std_str_copy ( char* dest, size_t dest_cap, const char* source );
int std_str_cmp ( const char* a, const char* b );
int std_str_cmp_part ( const char* a, const char* b, size_t size );
size_t std_str_format ( char* dest, size_t dest_cap, const char* source, ... );
size_t std_str_format_valist ( char* dest, size_t dest_cap, const char* source, va_list valist );
bool std_str_starts_with ( const char* str, const char* token );

#define std_str_copy_static_m( _dst, _src ) std_str_copy ( _dst, sizeof ( _dst ), _src )
#define std_str_format_static_m( _dst, _src, ... ) std_str_format ( _dst, sizeof ( _dst ), _src, __VA_ARGS__ )

// TODO support left pad as param
size_t std_u32_to_str ( char* str, size_t cap, uint32_t u32, uint32_t pad );
size_t std_u64_to_str ( char* str, size_t cap, uint64_t u64 );
size_t std_f32_to_str ( char* str, size_t cap, float f32 );

uint16_t std_str_to_u16 ( const char* str );
uint32_t std_str_to_u32 ( const char* str );
uint64_t std_str_to_u64 ( const char* str );
int32_t std_str_to_i32 ( const char* str );
int64_t std_str_to_i64 ( const char* str );
float std_str_to_f32 ( const char* str );

void std_u32_to_bin_str ( char* str, uint32_t u32 );
void std_u64_to_bin_str ( char* str, uint64_t u64 );

size_t std_str_trim_left  ( char* str, size_t str_len, const char** tokens, size_t tokens_count );
size_t std_str_trim_right ( char* str, size_t str_len, const char** tokens, size_t tokens_count );

char* std_str_find         ( const char* str, const char* token );
char* std_str_find_reverse ( const char* str, size_t str_start, const char* token );   // offset in byte count

size_t std_str_count ( const char* str, const char* token );

size_t std_str_replace ( char* str, const char* token, const char* new_token );
size_t std_str_copy_replace ( char* str, size_t cap, const char* source, const char* token, const char* new_token );

size_t std_size_to_str_approx ( char* dest, size_t cap, size_t size_value );
size_t std_count_to_str_approx ( char* dest, size_t cap, size_t count_value );

uint32_t std_str_hash_32 ( const char* str );
uint64_t std_str_hash_64 ( const char* str );

bool std_str_validate ( const char* str, size_t cap );
#define std_str_validate_m( _str ) ( std_str_validate ( _str, sizeof ( _str ) ) )

typedef struct {
    char* str;      // null terminated char string
    uint64_t len;   // terminator excluded
    uint64_t cap;   // terminator included
} std_string_t;

#define std_string_m( ... ) ( std_string_t ) { \
    .str = NULL, \
    .len = 0, \
    .cap = 0, \
    __VA_ARGS__ \
}
#define std_empty_string_m( s, c ) ({ if ( c > 0 ) ((char*)s)[0] = 0; std_string_m ( .str = s, .len = 0, .cap = c ); })
#define std_literal_string_m( s ) std_string_m ( .str = s, .len = sizeof ( s ) - 1, .cap = sizeof ( s ) )
#define std_static_string_m( s, ... ) std_string_m ( .str = s, .len = 0, .cap = sizeof ( s ), __VA_ARGS__ )
#define std_static_string_parse_m( s, ... ) std_string_m ( .str = s, .len = std_str_len ( s ), .cap = sizeof ( s ), __VA_ARGS__ )
#define std_fixed_string_m( s, c, ... ) std_string_m ( .str = (char*)s, .len = 0, .cap = c, __VA_ARGS__ )
#define std_fixed_string_len_m( s, n, ... ) std_string_m ( .str = (char*)s, .len = n, .cap = (n+1), __VA_ARGS__ )
#define std_fixed_string_parse_m( s, c, ... ) std_string_m ( .str = (char*)s, .len = std_str_len ( s ), .cap = c, __VA_ARGS__ )

bool std_string_copy ( std_string_t* string, const char* str );
bool std_string_append ( std_string_t* string, const char* str );
bool std_string_append_format ( std_string_t* string, const char* str, ... );
bool std_string_append_char ( std_string_t* string, char c );
bool std_string_pop ( std_string_t* string );
void std_string_clear ( std_string_t* string );
bool std_string_truncate_at ( std_string_t* string, const char* at ); // pointed character will become last in string
