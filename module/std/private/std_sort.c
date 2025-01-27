#include "std_sort.h"

#include <std_byte.h>
#include <std_time.h>

#include <stdlib.h>

std_xorshift64_state_t std_xorshift64_state ( void ) {
    std_xorshift64_state_t state;
    state.a = std_tick_now();
    return state;
}

uint64_t std_xorshift64 ( std_xorshift64_state_t* state ) {
    uint64_t x = state->a;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return state->a = x;
}

// https://rosettacode.org/wiki/Sorting_algorithms/Insertion_sort#C
void std_sort_insertion ( void* _base, size_t stride, size_t count, std_sort_comp_f* compare, const void* compare_arg, void* tmp ) {
    char* base = ( char* ) _base;

    for ( size_t i = 1; i < count; ++i ) {

        // Store the pivot in tmp
        std_mem_copy ( tmp, base + i * stride, stride );

        // Go back starting from the pivot and move the elements forward by one as long as they evaluate more than the pivot
        // This could overwrite the pivot, which is fine, because we have it stored in tmp
        size_t j = i;

        while ( j > 0 && compare ( base + ( j - 1 ) * stride, tmp, compare_arg ) > 0 ) {
            std_mem_copy ( base + j * stride, base + ( j - 1 ) * stride, stride );
            --j;
        }

        // Store the pivot where the last element that got moved forward was before being moved
        // This could store the pivot into itself if no elements were moved forward
        std_mem_copy ( base + j * stride, tmp, stride );
    }
}

void std_sort_insertion_copy ( void* _dest, const void* _base, size_t stride, size_t count, std_sort_comp_f* compare, const void* compare_arg ) {
    char* base = ( char* ) _base;
    char* dest = ( char* ) _dest;

    std_mem_copy ( dest, base, stride );

    for ( size_t i = 1; i < count; ++i ) {
        // Go back starting from the pivot and move the elements forward by one as long as they evaluate more than the pivot
        // This could overwrite the pivot, which is fine, because we have it stored in tmp
        size_t j = i;

        while ( j > 0 && compare ( dest + ( j - 1 ) * stride, base + i * stride, compare_arg ) > 0 ) {
            std_mem_copy ( dest + j * stride, dest + ( j - 1 ) * stride, stride );
            --j;
        }

        // Store the pivot where the last element that got moved forward was before being moved
        // This could store the pivot into itself if no elements were moved forward
        std_mem_copy ( dest + j * stride, base + i * stride, stride );
    }
}

// https://rosettacode.org/wiki/Sorting_algorithms/Quicksort
static void std_sort_quick_rec ( void* _base, size_t stride, size_t a, size_t b, std_sort_comp_f* compare, const void* compare_arg, void* tmp ) {
    char* base = ( char* ) ( _base );

    if ( a == b ) {
        return;
    }

    // partition
    size_t i = a;
    size_t j = b;
    void* pivot = base + i * stride;

    while ( i < j ) {
        while ( compare ( base + j * stride, pivot, compare_arg ) > 0 ) {
            --j;
        }

        while ( compare ( base + i * stride, pivot, compare_arg ) < 0 ) {
            ++i;
        }

        if ( i < j ) {
            // Swap i and j
            std_mem_copy ( tmp, base + i * stride, stride );
            std_mem_copy ( base + i * stride, base + j * stride, stride );
            std_mem_copy ( base + j * stride, tmp, stride );
            --j;
            ++i;
        }
    }

    // Swap a and j
    std_mem_copy ( tmp, base + a * stride, stride );
    std_mem_copy ( base + a * stride, base + j * stride, stride );
    std_mem_copy ( base + j * stride, tmp, stride );

    // rec call
    std_sort_quick_rec ( base, stride, a, j, compare, compare_arg, tmp );
    std_sort_quick_rec ( base, stride, j + 1, b, compare, compare_arg, tmp );
}

// TODO test this
void std_sort_quick ( void* base, size_t stride, size_t count, std_sort_comp_f* compare, const void* compare_arg, void* tmp ) {
    std_sort_quick_rec ( base, stride, 0, count - 1, compare, compare_arg, tmp );
}

// Fisher–Yates shuffle
void std_sort_shuffle ( std_xorshift64_state_t* rng, void* base, size_t stride, size_t count, void* tmp ) {
    uint64_t i = count;

    while (i != 0) {
        uint64_t j = std_xorshift64 ( rng ) % i;
        i--;

        std_mem_copy ( tmp, base + i * stride, stride );
        std_mem_copy ( base + i * stride, base + j * stride, stride);
        std_mem_copy ( base + j * stride, tmp, stride );
    }
}
