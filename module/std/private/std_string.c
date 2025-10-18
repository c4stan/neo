#include <std_string.h>

#include <std_byte.h>
#include <std_log.h>
#include <std_allocator.h>

#include <math.h>
#include <stdlib.h>

#include "std_state.h"

#include <stdio.h>

//==============================================================================

size_t std_str_count_ascii ( const char* str ) {
    return strlen ( str );
}

bool std_utf8_is_single_byte ( char c ) {
    return ( c & 0x80 ) == 0x00;
}

bool std_utf8_is_double_byte ( char c ) {
    return ( c & 0xe0 ) == 0xc0;
}

bool std_utf8_is_triple_byte ( char c ) {
    return ( c & 0xf0 ) == 0xe0;
}

bool std_utf8_is_quadruple_byte ( char c ) {
    return ( c & 0xf8 ) == 0xf0;
}

bool std_utf8_is_first_byte ( char c ) {
    return ( c & 0xc0 ) != 0x80;
}

size_t std_utf8_char_size ( char c ) {
    if ( std_utf8_is_single_byte ( c ) ) {
        return 1;
    } else if ( std_utf8_is_double_byte ( c ) ) {
        return 2;
    } else if ( std_utf8_is_triple_byte ( c ) ) {
        return 3;
    } else if ( std_utf8_is_quadruple_byte ( c ) ) {
        return 4;
    } else {
        std_log_error_m ( "Malformed UTF8 character" );
        return 0;
    }
}

size_t std_utf8_char_size_reverse ( const char* last_byte, const char* base ) {
    std_assert_m ( last_byte != NULL );
    const char* ptr = last_byte;

    bool found = std_utf8_is_first_byte ( *ptr );
    while ( !found && ptr > base ) {
        --ptr;
        found = std_utf8_is_first_byte ( *ptr );
    }

    if ( found ) {
        return std_utf8_char_size ( *ptr );
    } else {
        std_log_error_m ( "Malformed UTF8 character" );
        return 0;
    }
}

size_t std_str_count_utf8 ( const char* str ) {
    size_t len = 0;

    while ( *str != '\0' ) {
        str += std_utf8_char_size ( *str );
        ++len;
    }

    return len;
}

size_t std_str_len ( const char* str ) {
    return strlen ( str );
}

size_t std_str_copy ( char* dest, size_t cap, const char* src ) {
    std_assert_m ( cap > 0 );
    size_t i = 0;

    for ( ;; ) {
        if ( i == cap ) {
            if ( cap > 0 ) {
                dest[cap - 1] = '\0';
            }

            return std_str_len ( src );
        }

        dest[i] = src[i];

        if ( src[i] == '\0' ) {
            return i;
        }

        ++i;
    }
}

//size_t std_str_append ( char* dest, size_t dest_cap, const char* source ) {
//void std_str_append ( char* dest, std_array_t* array, const char* source ) {
//    //size_t len = std_str_len ( dest );
//    //std_assert_m ( len < dest_cap );
//    //return std_str_copy ( dest + len, dest_cap - len, source );
//    uint64_t capacity = array->capacity - array->count;
//    uint64_t source_len = std_str_copy ( dest + array->count, capacity, source );
//    std_assert_m ( capacity > source_len );
//    std_array_push ( array, source_len );
//}

int std_str_cmp ( const char* a, const char* b ) {
    return strcmp ( a, b );
}

int std_str_cmp_part ( const char* a, const char* b, size_t size ) {
    return strncmp ( a, b, size );
}

bool std_str_starts_with ( const char* str, const char* token ) {
    while ( *token != '\0' ) {
        if ( *str == '\0' ) {
            return false;
        } else if ( *str != *token ) {
            return false;
        }

        ++str;
        ++token;
    }

    return true;
}

size_t std_str_format ( char* dest, size_t cap, const char* src, ... ) {
    va_list va;
    va_start ( va, src );
    int result = vsnprintf ( dest, cap, src, va );
    va_end ( va );

    if ( result < 0 ) {
        result = 0;
    }

    return ( size_t ) result;
}

size_t std_str_format_valist ( char* dest, size_t cap, const char* src, va_list valist ) {
    int result = vsnprintf ( dest, cap, src, valist );

    if ( result < 0 ) {
        result = 0;
    }

    return ( size_t ) result;
}

size_t std_u32_to_str ( char* str, size_t size, uint32_t u32, uint32_t pad ) {
    int len = snprintf ( NULL, 0, "%u", u32 );
    if ( len >= size ) {
        return SIZE_MAX;
    }
    if ( pad < len ) {
        pad = len;
    }

    std_mem_set ( str, pad, ' ' );
    str[pad] = '\0';    
    snprintf ( str + pad - len, size, "%u", u32 );
    return ( size_t ) len;
}

size_t std_u64_to_str ( char* str, size_t size, uint64_t u64 ) {
    int len = snprintf ( str, size, "%llu", ( unsigned long long ) u64 );
    return len < 0 ? SIZE_MAX : ( size_t ) len;
}

uint16_t std_str_to_u16 ( const char* str ) {
    return ( uint16_t ) strtoul ( str, NULL, 10 );
}

uint32_t std_str_to_u32 ( const char* str ) {
    return ( uint32_t ) strtoul ( str, NULL, 10 );
}

uint64_t std_str_to_u64 ( const char* str ) {
    return ( uint64_t ) strtoull ( str, NULL, 10 );
}

int32_t std_str_to_i32 ( const char* str ) {
    return ( int32_t ) strtol ( str, NULL, 10 );
}

int64_t std_str_to_i64 ( const char* str ) {
    return ( int64_t ) strtoll ( str, NULL, 10 );
}

float std_str_to_f32 ( const char* str ) {
    // The code uses doubles and converts to float at the end to minimize the error introduced.
    // The conversion accuracy is probably not perfect in theory, but in practice its decent
    // and it's pretty fast when compared to sscanf (~10x).
    const char* p = str;
    // Parse sign
    char sign = '+';

    if ( *p == '+' || *p == '-' ) {
        sign = *p;
        ++p;
    }

    // Read int part
    double mantissa = 0.0;

    while ( *p != 'e' && *p != 'E' && *p != '.' && *p != ' ' && *p != '\0' ) {
        mantissa *= 10.0;
        mantissa += *p - '0';
        ++p;
    }

    // Read decimal part
    if ( *p == '.' ) {
        ++p;
        double frac = 1.0;

        while ( *p != 'e' && *p != 'E' && *p != ' ' && *p != '\0' ) {
            frac *= 0.1;
            mantissa += ( *p - '0' ) * frac;
            ++p;
        }
    }

    // Read exp
    int exp = 0;
    char exp_sign = '+';

    if ( *p == 'e' || *p == 'E' ) {
        ++p;

        // Read exp sign
        if ( *p == '+' || *p == '-' ) {
            exp_sign = *p;
            ++p;
        }

        // Read exp value
        while ( *p != ' ' && *p != '\0' ) {
            exp *= 10;
            exp += *p - '0';
            ++p;
        }
    }

    // Assemble
    double a = pow ( 5, exp );
    double b = pow ( 2, exp );

    if ( exp_sign == '-' ) {
        a = 1.0 / a;
        b = 1.0 / b;
    }

    return ( float ) ( ( sign == '+' ? 1.0 : -1.0 ) * mantissa * a * b );
}

size_t std_f32_to_str ( char* str, size_t cap, float f32 ) {
    // TODO take decimals # as param
    int len = snprintf ( str, cap, "%.2f", f32 );
    return len < 0 ? SIZE_MAX : ( size_t ) len;
}

void std_u32_to_bin_str ( char* str, uint32_t u32 ) {
    str += 32;
    *str-- = '\0';

    for ( int i = 31; i >= 0; i-- ) {
        *str-- = ( u32 & 1 ) + '0';

        u32 >>= 1;
    }
}

void std_u64_to_bin_str ( char* str, uint64_t u64 ) {
    str += 64;
    *str-- = '\0';

    for ( int i = 63; i >= 0; i-- ) {
        *str-- = ( u64 & 1 ) + '0';

        u64 >>= 1;
    }
}

size_t std_str_trim_left ( char* str, size_t str_len, const char** tokens, size_t tokens_count ) {
    size_t str_begin = 0;
    bool match;

    do {
        match = false;

        for ( size_t i = 0; i < tokens_count; ++i ) {
            const char* token = tokens[i];
            size_t token_len = std_str_len ( token );

            if ( str_len - str_begin < token_len ) {
                continue;
            }

            if ( std_mem_cmp ( str + str_begin, token, token_len ) == 0 ) {
                match = true;
                str_begin += token_len;
                break;
            }
        }
    } while ( match );

    // exclude initial trimmed piece, include terminator, return new len
    std_mem_move ( str, str + str_begin, str_len - str_begin + 1 );
    return str_len - str_begin;
}

size_t std_str_trim_right ( char* str, size_t str_len, const char** tokens, size_t tokens_count ) {
    bool match;

    do {
        match = false;

        for ( size_t i = 0; i < tokens_count; ++i ) {
            const char* token = tokens[i];
            size_t token_len = std_str_len ( token );

            if ( str_len < token_len ) {
                continue;
            }

            if ( std_mem_cmp ( str + str_len - token_len, token, token_len ) == 0 ) {
                match = true;
                str_len -= token_len;
                break;
            }
        }
    } while ( match );

    // trim by inserting a terminator, return new len
    str[str_len] = '\0';
    return str_len;
}

char* std_str_find ( const char* str, const char* token ) {
    // Naive token match for every char in str
    size_t i = 0;

    while ( str[i] != '\0' ) {
        if ( std_str_cmp ( str + i, token ) == 0 ) {
        //if ( std_mem_cmp ( str + i, token, token_len ) == 0 ) {
            return ( char* ) ( str + i );
        }

        ++i;
    }

    return NULL;
}

char* std_str_find_reverse ( const char* str, size_t offset, const char* token ) {
    // Naive token match for every char in str
    size_t token_len = std_str_len ( token );

    if ( offset < token_len ) {
        return NULL;
    }

    // add 1 to account for the initial sub inside the following while
    size_t i = offset + 1 - token_len;

    while ( i > 0 ) {
        --i;

        if ( std_mem_cmp ( str + i, token, token_len ) == 0 ) {
            return ( char* ) ( str + i );
        }
    }

    return NULL;
}

size_t std_str_count ( const char* str, const char* token ) {
    size_t token_len = std_str_len ( token );
    size_t count = 0;

    char* find = ( char* ) str;
    while ( ( find = std_str_find ( find, token ) ) ) {
        find += token_len;
        ++count;
    }

    return count;
}

size_t std_str_replace ( char* str, const char* token, const char* new_token ) {
    size_t token_len = std_str_len ( token );
    size_t new_token_len = std_str_len ( new_token );

    if ( token_len != new_token_len ) {
        // TODO do better?
        std_log_error_m ( "Trying to call std_str_replace with different sized tokens. Use std_str_copy_replace instead." );
        return 0;
    }

    size_t count = 0;

    char* find = str;
    while ( find = std_str_find ( find, token ) ) {
        std_mem_copy ( find, new_token, token_len );
        find += token_len;
    }

    return count;
}

size_t std_str_copy_replace ( char* dest, size_t cap, const char* source, const char* token, const char* new_token ) {
    size_t token_len = std_str_len ( token );
    size_t new_token_len = std_str_len ( new_token );

    size_t count = 0;

    char* find;
    while ( find = std_str_find ( source, token ) ) {
        size_t offset = find - source;
        std_mem_copy ( dest, source, offset );
        std_mem_copy ( dest + offset, new_token, new_token_len );
        dest += offset + new_token_len;
        source += offset + token_len;
    }

    return count;
}

static size_t std_u64_to_str_approx ( char* dest, size_t cap, uint64_t u64, uint32_t token_count, const char** tokens, const uint64_t u64_max ) {
    uint64_t multiplier = u64_max;

    for ( size_t i = 0; i < token_count; ++i, multiplier = multiplier >> 10 ) {
        if ( u64 < multiplier ) {
            continue;
        }

        int retval;

        if ( u64 % multiplier == 0 ) {
            retval = snprintf ( dest, cap, std_fmt_u64_m " " std_fmt_str_m, u64 / multiplier, tokens[i] );
        } else {
            // The ~ character breaks a number of fonts when used for text rendering... better if avoided
            //retval = snprintf ( dest, cap, "~" std_fmt_f32_dec_m ( 1 ) " " std_fmt_str_m, ( float ) u64 / multiplier, tokens[i] );
            retval = snprintf ( dest, cap, std_fmt_f32_dec_m ( 1 ) " " std_fmt_str_m, ( float ) u64 / multiplier, tokens[i] );
        }

        std_assert_m ( retval >= 0 );
        return ( size_t ) retval;
    }

    return std_str_copy ( dest, cap, "0" );
}

size_t std_count_to_str_approx ( char* dest, size_t cap, size_t count_value ) {
    static const char*      units[] = { "B", "M", "K", "" };
    static const uint64_t   max = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
    return std_u64_to_str_approx ( dest, cap, count_value, std_static_array_capacity_m ( units ), units, max );
}

size_t std_size_to_str_approx ( char* dest, size_t cap, size_t size_value ) {
    //static const char*      units[] = { "EiB", "PiB", "TiB", "GiB", "MiB", "KiB", "B" };
    static const char*      units[] = { "EB", "PB", "TB", "GB", "MB", "KB", "B" };
    static const uint64_t   max = 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;
    return std_u64_to_str_approx ( dest, cap, size_value, std_static_array_capacity_m ( units ), units, max );
}

uint32_t std_str_hash_32 ( const char* str ) {
    uint32_t hash = 5381;
    char c;

    while ( c = *str++ ) {
        hash = hash * 33 ^ c;
    }

    return hash;
}

uint64_t std_str_hash_64 ( const char* str ) {
    uint64_t hash = 5381;
    char c;

    while ( c = *str++ ) {
        hash = hash * 33 ^ c;
    }

    return hash;
}

bool std_str_validate ( const char* str, size_t cap ) {
    bool terminated = false;

    for ( size_t i = 0; i < cap; ++i ) {
        if ( str[i] == '\0' ) {
            terminated = true;
            break;
        }
    }

    return terminated;
}

//const char* std_str_static ( const char* text, size_t size ) {
//    // TODO avoid leaking this memory
//    std_alloc_t alloc = std_virtual_heap_alloc ( size, 16 );
//    std_auto_m str = ( const char* ) alloc.buffer.base;
//    std_str_copy ( str, size, text );
//    return str;
//}
