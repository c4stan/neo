#pragma once

#include <std_platform.h>

typedef struct {
    uint64_t count;
    uint64_t capacity;
} std_array_t;

#define std_static_array_m( _array ) std_array ( std_static_array_capacity_m ( _array ) )

std_array_t std_array ( uint64_t capacity );
void std_array_push ( std_array_t* array, uint64_t count );
void std_array_pop ( std_array_t* array, uint64_t count );
