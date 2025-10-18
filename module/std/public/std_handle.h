#pragma once

#include <std_platform.h>

#define std_define_handle_m( name ) typedef union { uint64_t u64; struct { uint32_t gen; uint32_t idx; }; } name
#define std_null_handle_m( name ) ( name ) { .gen = 0, .idx = UINT32_MAX }
#define std_handle_is_equal_m( h1, h2 ) ( h1.u64 == h2.u64 )
#define std_handle_is_null_m( h ) ( h.gen == 0 && h.idx == UINT32_MAX )
typedef uint32_t std_handle_gen_t;
