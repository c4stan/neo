#pragma once

#include <std_platform.h>
#include <std_allocator.h>

#if 0
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

#else

// TODO remove tag once Clang support C23 proposal N3037 (Improved tag compatibility)
#define std_array_type_m( type ) struct array_ ## type { type* data; uint64_t count; uint64_t capacity; }
#define std_array_m( type, ... ) ( std_array_type_m ( type ) ) { \
    .data = NULL, \
    .count = 0, \
    .capacity = 0, \
    __VA_ARGS__ \
}
#define std_static_array_m( type, array ) std_array_m ( type, .data = array, .capacity = std_static_array_capacity_m ( array ) )

#define std_array_push_m( array, item ) std_assert_m ( (array)->count < (array)->capacity ); (array)->data[(array)->count++] = item;
#define std_array_pop_m( array ) ( { std_assert_m ( (array)->count > 0 ); (array)->data[--((array)->count)]; } )

#endif
