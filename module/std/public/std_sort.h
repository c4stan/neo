#pragma once

#include <std_platform.h>
#include <std_compiler.h>

typedef struct {
    uint64_t a;
} std_xorshift64_state_t;

std_xorshift64_state_t std_xorshift64_state ( void );
uint64_t std_xorshift64 ( std_xorshift64_state_t* state );

// Return value:
// < 0 => a before b
// > 0 => a after b
//   0 => a and b maintain input order if stable, unspecified if not
typedef int ( std_sort_comp_f ) ( const void* a, const void* b );

// tmp is used to temporarily store the pivot element while sorting
void std_sort_insertion ( void* base, size_t stride, size_t count, std_sort_comp_f* compare, void* tmp );

// out of place insertion sort
void std_sort_insertion_copy ( void* dest, const void* base, size_t stride, size_t count, std_sort_comp_f* compare );

void std_sort_quick ( void* base, size_t stride, size_t count, std_sort_comp_f compare, void* tmp );

void std_sort_shuffle ( std_xorshift64_state_t* rng, void* base, size_t stride, size_t count, void* tmp );
