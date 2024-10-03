#pragma once

#include <std_platform.h>
#include <std_allocator.h>

#define std_array_type_m( type ) struct std_pp_eval_concat_m ( std_array_, type ) { type* data; uint64_t count; uint64_t capacity; }

#define std_array_m( type, ... ) ( std_array_type_m ( type ) ) { \
    .data = NULL, \
    .count = 0, \
    .capacity = 0, \
    __VA_ARGS__ \
}

#define std_heap_array_m( type, cap ) std_array_m ( type, .data = std_virtual_heap_alloc_array_m ( type, cap ) )
#define std_static_array_m( type, array ) std_array_m ( type, .data = array, .capacity = std_static_array_capacity_m ( array ) )

// TODO heap_array api
//#define std_heap_array_emplace_m( array, item )
