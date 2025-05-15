#pragma once

#include <std_platform.h>
#include <std_array.h>

#define std_str_find_null_m    SIZE_MAX

// String length is to be intended as the result of strlen(), even on utf8 strings. It is the byte size of the string without the terminator.
// To know how many actual single readable characters are encoded in a string, use str_count.
// Capacity(cap) parameters are to be specified in raw byte size, including the terminator character.

/*
    options for a memcpy-like string api
        return enum
            a bit of an extreme approach that would have to be enforced all around the api to make sense, also a bit out of place for simple functions like memcpy
        return size that you need to complete the op, and client checks that against provided capacity to infer the result
            what snprintf uses
        return size that you moved
            broken, can't know if the capacity was not enough
        return bool
            doesn't let the client know how much size is required to complete successfully
*/

// TODO add a string view (char* begin, char* end) type and use that in most places when taking in a string as param instead of a single null terminated char*

bool std_utf8_is_single_byte    ( char c );
bool std_utf8_is_double_byte    ( char c );
bool std_utf8_is_triple_byte    ( char c );
bool std_utf8_is_quadruple_byte ( char c );
size_t std_utf8_char_size         ( char first_byte );
size_t std_utf8_char_size_reverse ( const char* last_byte, size_t back_len );  // back_len is distance from string base to last_byte

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
#define std_str_format_m( _dst, _src, ... ) std_str_format ( _dst, sizeof ( _dst ), _src, __VA_ARGS__ )

// TODO support left pad as param
size_t std_u32_to_str ( char* str, size_t cap, uint32_t u32, uint32_t pad );
size_t std_u64_to_str ( char* str, size_t cap, uint64_t u64 );
size_t std_f32_to_str ( float f32, char* str, size_t cap );

uint32_t std_str_to_u32 ( const char* str );
uint64_t std_str_to_u64 ( const char* str );
int32_t std_str_to_i32 ( const char* str );
int64_t std_str_to_i64 ( const char* str );
float std_str_to_f32 ( const char* str );

// TODO add _str_ somewhere in the functions name
void std_u32_to_bin ( uint32_t u32, char* str );
void std_u64_to_bin ( uint64_t u64, char* str );

size_t std_str_trim_left  ( char* str, const char** tokens, size_t n );
size_t std_str_trim_right ( char* str, const char** tokens, size_t n );

// TODO return ptr?
size_t std_str_find         ( const char* str, const char* token );
size_t std_str_find_reverse ( const char* str, size_t start, const char* token );   // offset in byte count

size_t std_str_count ( const char* str, const char* token );

size_t std_str_replace ( char* str, const char* token, const char* new_token );
// TODO
//size_t std_str_copy_replace ( char* new_str, size_t cap, const char* str, const char* token, const char* new_token );

size_t std_size_to_str_approx ( char* dest, size_t cap, size_t size_value );
size_t std_count_to_str_approx ( char* dest, size_t cap, size_t count_value );

//void std_str_append ( char* dest, std_array_t* array, const char* source );

uint32_t std_str_hash_32 ( const char* str );
uint64_t std_str_hash_64 ( const char* str );

bool std_str_validate ( const char* str, size_t cap );
#define std_str_validate_m( _str ) ( std_str_validate ( _str, sizeof ( _str ) ) )
