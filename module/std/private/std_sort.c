#include "std_sort.h"

#include <std_byte.h>

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
