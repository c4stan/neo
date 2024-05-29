#include <std_string.h>

#include <std_byte.h>
#include <std_log.h>
#include <std_allocator.h>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "std_state.h"

//==============================================================================

//static std_string_state_t* std_string_state;

//==============================================================================

//void std_string_init ( std_string_state_t* state ) {
//    state->static_string_table_buffer = std_virtual_buffer_reserve ( std_string_static_table_max_size_m );
//}

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

size_t std_utf8_char_size ( char c ) {
    if ( std_utf8_is_single_byte ( c ) ) {
        return 1;
    } else if ( std_utf8_is_double_byte ( c ) ) {
        return 2;
    } else if ( std_utf8_is_triple_byte ( c ) ) {
        return 3;
    } else {
        return 4;
    }
}

size_t std_utf8_char_size_reverse ( const char* c, size_t back_len ) {
    std_assert_m ( c != NULL );
#if std_log_assert_enabled_m
    const char* base = c - back_len;
#endif
    const char* ptr = c;

    for ( ;; ) {
        std_assert_m ( ptr >= base );

        if ( std_utf8_is_single_byte ( *ptr ) ) {
            break;
        }

        --ptr;
    }

    return ( size_t ) ( c - ptr ) + 1;
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
    return strlen ( str );  // TODO
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

size_t std_u32_to_str ( uint32_t u32, char* str, size_t size ) {
    int len = snprintf ( str, size, "%u", u32 );
    return len < 0 ? SIZE_MAX : ( size_t ) len;
}

size_t std_u64_to_str ( uint64_t u64, char* str, size_t size ) {
    int len = snprintf ( str, size, "%llu", ( unsigned long long ) u64 );
    return len < 0 ? SIZE_MAX : ( size_t ) len;
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

size_t std_f32_to_str ( float f32, char* str, size_t cap ) {
    // TODO take decimals # as param
    int len = snprintf ( str, cap, "%.2f", f32 );
    return len < 0 ? SIZE_MAX : ( size_t ) len;
}

void std_u32_to_bin ( uint32_t u32, char* str ) {
    str += 32;
    *str-- = '\0';

    for ( int i = 31; i >= 0; i-- ) {
        *str-- = ( u32 & 1 ) + '0';

        u32 >>= 1;
    }
}

void std_u64_to_bin ( uint64_t u64, char* str ) {
    str += 64;
    *str-- = '\0';

    for ( int i = 63; i >= 0; i-- ) {
        *str-- = ( u64 & 1 ) + '0';

        u64 >>= 1;
    }
}

size_t std_str_trim_left ( char* str, const char** tokens, size_t n ) {
    size_t str_len = std_str_len ( str );
    size_t str_begin = 0;
    bool match;

    do {
        match = false;

        for ( size_t i = 0; i < n; ++i ) {
            const char* token = tokens[i];
            size_t token_len = std_str_len ( token );

            if ( str_len - str_begin < token_len ) {
                continue;
            }

            if ( std_mem_cmp ( str + str_begin, token, token_len ) ) {
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

size_t std_str_trim_right ( char* str, const char** tokens, size_t n ) {
    size_t str_len = std_str_len ( str );
    bool match;

    do {
        match = false;

        for ( size_t i = 0; i < n; ++i ) {
            const char* token = tokens[i];
            size_t token_len = std_str_len ( token );

            if ( str_len < token_len ) {
                continue;
            }

            if ( std_mem_cmp ( str + str_len - token_len, token, token_len ) ) {
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

size_t std_str_find ( const char* str, const char* token ) {
    // Naive token match for every char in str
    size_t str_len = std_str_len ( str );
    size_t token_len = std_str_len ( token );
    size_t i = 0;

    if ( token_len > str_len ) {
        return std_str_find_null_m;
    }

    while ( i <= str_len - token_len ) {
        if ( std_mem_cmp ( str + i, token, token_len ) ) {
            return i;
        }

        ++i;
    }

    return std_str_find_null_m;
}

size_t std_str_find_reverse ( const char* str, size_t offset, const char* token ) {
    // Naive token match for every char in str
    size_t token_len = std_str_len ( token );

    if ( offset < token_len ) {
        return std_str_find_null_m;
    }

    // add 1 to account for the initial sub inside the following while
    size_t i = offset + 1 - token_len;

    while ( i > 0 ) {
        --i;

        if ( std_mem_cmp ( str + i, token, token_len ) ) {
            return i;
        }
    }

    return std_str_find_null_m;
}

size_t std_str_count ( const char* str, const char* token ) {
    size_t token_len = std_str_len ( token );
    size_t count = 0;
    size_t i = 0;

    while ( ( i = std_str_find ( str + i, token ) ) != std_str_find_null_m ) {
        ++count;
        i += token_len;
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
            retval = snprintf ( dest, cap, "~" std_fmt_f32_dec_m ( 1 ) " " std_fmt_str_m, ( float ) u64 / multiplier, tokens[i] );
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
