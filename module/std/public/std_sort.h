#pragma once

#include <std_platform.h>
#include <std_compiler.h>

// Return value:
// < 0 -> a before b
// > 0 -> a after b
//   0 -> a and b maintain input order if stable, unspecified if not
typedef int ( std_sort_comp_f ) ( const void* a, const void* b, const void* arg );

// tmp is used to temporarily store the pivot element while sorting
void std_sort_insertion ( void* base, size_t stride, size_t count, std_sort_comp_f* compare, const void* compare_arg, void* tmp );

// out of place insertion sort
void std_sort_insertion_copy ( void* dest, const void* base, size_t stride, size_t count, std_sort_comp_f* compare, const void* compare_arg );
