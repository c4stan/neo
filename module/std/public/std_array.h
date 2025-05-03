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

// TODO waiting for Clang to support C23 improved tag compatibility (N3037)...
#if 0 && defined(std_compiler_gcc_m)
#define std_array_type_m( type ) struct array_ ## type { type* data; uint64_t count; uint64_t capacity; }
#else
#define std_array_type_m( type ) struct std_pp_eval_concat_m ( array_, std_pp_eval_concat_m ( type, __LINE__ ) ) { type* data; uint64_t count; uint64_t capacity; }
#endif
#define std_array_m( type, ... ) ( std_array_type_m ( type ) ) { \
    .data = NULL, \
    .count = 0, \
    .capacity = 0, \
    ##__VA_ARGS__ \
}
#define std_static_array_m( type, array ) std_array_m ( type, .data = array, .capacity = std_static_array_capacity_m ( array ) )
#define std_array_push_m( array, item ) std_assert_m ( (array)->count < (array)->capacity ); (array)->data[(array)->count++] = item;
#define std_array_pop_m( array ) ( { std_assert_m ( (array)->count > 0 ); (array)->data[--((array)->count)]; } )

#define std_heap_array_create_m( type, cap ) std_array_m ( type, .data = std_virtual_heap_alloc_array_m ( type, cap ) )
#define std_heap_array_destroy_m( array ) std_virtual_heap_free ( array.data )
// TODO heap array api

#endif
