#pragma once

#include <std_platform.h>
#include <std_compiler.h>

typedef int ( std_sort_comp_f ) ( const void* a, const void* b );

// tmp is used to temporarily store the pivot element while sorting
void std_sort_insertion ( void* base, size_t stride, size_t count, std_sort_comp_f* compare, void* tmp );

// out of place insertion sort
void std_sort_insertion_copy ( void* dest, const void* base, size_t stride, size_t count, std_sort_comp_f* compare );

void std_sort_quick ( void* base, size_t stride, size_t count, std_sort_comp_f compare, void* tmp );
