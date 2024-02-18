#include <std_main.h>

#include <std_log.h>
#include <std_allocator.h>
#include <std_time.h>
#include <std_compiler.h>

#include <vm_vector.h>

#include <stdio.h>

void test_platform ( void ) {
    uint32_t sse = VM_SSE;
    uint32_t avx = VM_AVX;
    uint32_t avx512 = VM_AVX512;
    std_log_info_m ( "SSE: " std_fmt_u32_m " AVX: " std_fmt_u32_m " AVX512: " std_fmt_u32_m, sse, avx, avx512 );
}

void test_sum ( void ) {
    uint32_t count = 4 * 1448;
    size_t page_size = std_virtual_page_size();
    size_t size = std_align ( sizeof ( float ) * count, page_size );
    std_alloc_t alloc = std_virtual_alloc ( size );
    float* f1 = ( float* ) alloc.buffer.base;

    for ( uint32_t i = 0; i < count; ++i ) {
        f1[i] = ( float ) i;
    }

    std_tick_t t1 = std_tick_now();

    float r1 = 0;

    for ( uint32_t i = 0; i < count; ++i ) {
        r1 += f1[i];
    }

    std_tick_t t2 = std_tick_now();

    vm_vec_4f_t vr1 = vm_vec_4f_zero();

    for ( uint32_t i = 0; i < count; i += 4 ) {
        vm_vec_4f_t v = vm_vec_4f_load ( f1 + i );
        vr1 = vm_vec_4f_add ( vr1, v );
    }

    std_tick_t t3 = std_tick_now();

    std_log_info_m ( std_fmt_tick_m ": " std_fmt_u32_m, t2 - t1, r1 );

    std_static_align_m ( 16 ) float vrf[4];
    vm_vec_4f_store ( vrf, vr1 );

    std_log_info_m ( std_fmt_tick_m ": " std_fmt_u32_m, t3 - t2, vrf[0] + vrf[1] + vrf[2] + vrf[3] );
}

void std_main ( void ) {
    test_platform();
    test_sum();
    return;
}
