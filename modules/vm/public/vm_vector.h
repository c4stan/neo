#pragma once

#include <std_compiler.h>

// TODO: move the platform stuff into a separate header, the SSE stuff into another separate header (one per SSE/SSE2/AVX/...),
// have this be a "generic" user header that auto includes the SSE headers available on the platform
// and maybe implements some more advanced vector operations?

// ======================================================================================= //
//                                     P L A T F O R M
// ======================================================================================= //
/*
from https://stackoverflow.com/questions/11228855/header-files-for-x86-simd-intrinsics
+----------------+------------------------------------------------------------------------------------------+
|     Header     |                                         Purpose                                          |
+----------------+------------------------------------------------------------------------------------------+
| x86intrin.h    | Everything, including non-vector x86 instructions like _rdtsc().                         |
| mmintrin.h     | MMX (Pentium MMX!)                                                                       |
| mm3dnow.h      | 3dnow! (K6-2) (deprecated)                                                               |
| xmmintrin.h    | SSE + MMX (Pentium 3, Athlon XP)                                                         |
| emmintrin.h    | SSE2 + SSE + MMX (Pentium 4, Athlon 64)                                                  |
| pmmintrin.h    | SSE3 + SSE2 + SSE + MMX (Pentium 4 Prescott, Athlon 64 San Diego)                        |
| tmmintrin.h    | SSSE3 + SSE3 + SSE2 + SSE + MMX (Core 2, Bulldozer)                                      |
| popcntintrin.h | POPCNT (Nehalem (Core i7), Phenom)                                                       |
| ammintrin.h    | SSE4A + SSE3 + SSE2 + SSE + MMX (AMD-only, starting with Phenom)                         |
| smmintrin.h    | SSE4_1 + SSSE3 + SSE3 + SSE2 + SSE + MMX (Penryn, Bulldozer)                             |
| nmmintrin.h    | SSE4_2 + SSE4_1 + SSSE3 + SSE3 + SSE2 + SSE + MMX (Nehalem (aka Core i7), Bulldozer)     |
| wmmintrin.h    | AES (Core i7 Westmere, Bulldozer)                                                        |
| immintrin.h    | AVX, AVX2, AVX512, all SSE+MMX (except SSE4A and XOP), popcnt, BMI/BMI2, FMA             |
+----------------+------------------------------------------------------------------------------------------+
*/

// TODO
#include <immintrin.h>

// From https://github.com/vectorclass/version2/blob/master/instrset.h
/*
#if defined ( __AVX512VL__ ) && defined ( __AVX512BW__ ) && defined ( __AVX512DQ__ )
#define INSTRSET 10
#elif defined ( __AVX512F__ ) || defined ( __AVX512__ )
#define INSTRSET 9
#elif defined ( __AVX2__ )
#define INSTRSET 8
#elif defined ( __AVX__ )
#define INSTRSET 7
#elif defined ( __SSE4_2__ )
#define INSTRSET 6
#elif defined ( __SSE4_1__ )
#define INSTRSET 5
#elif defined ( __SSSE3__ )
#define INSTRSET 4
#elif defined ( __SSE3__ )
#define INSTRSET 3
#elif defined ( __SSE2__ ) || defined ( __x86_64__ )
#define INSTRSET 2
#elif defined ( __SSE__ )
#define INSTRSET 1
#elif defined ( _M_IX86_FP )           // Defined in MS compiler. 1: SSE, 2: SSE2
#define INSTRSET _M_IX86_FP
#else
#define INSTRSET 0
#endif // instruction set defines
*/

// SSE
#if defined ( __SSE4_1__ ) || defined ( __SSE4_2__ )
    #define VM_SSE 4
#elif defined ( __SSE3__ ) || defined ( __SSSE3__ )
    #define VM_SSE 3
#elif defined ( __SSE2__ )
    #define VM_SSE 2
#elif defined ( __SSE__ )
    #define VM_SSE 1
#else
    #define VM_SSE 0
#endif

#if defined ( __SSSE3__ )
    #define VM_SSE_3_S
#endif

#if defined ( __SSE4_1__ )
    #define VM_SSE_4_1
#endif

#if defined ( __SSE4_2__ )
    #define VM_SSE_4_2
#endif

// AVX
#if defined ( __AVX2__ )
    #define VM_AVX 2
#elif defined ( __AVX__ )
    #define VM_AVX 1
#else
    #define VM_AVX 0
#endif

// AVX-512
// TODO
#define VM_AVX512 0

#if VM_SSE > 0
    typedef __m128 vm_vec_4f_t;
#endif

#if VM_AVX > 0
    typedef __m256 vm_vec_8f_t;
#endif

#if VM_AVX512 > 0
    typedef __m512 vm_vec_16f_t;
#endif

#define VM_API std_inline_m static

// ======================================================================================= //
//                                         S S E 1
// ======================================================================================= //
#if VM_SSE > 0

    // Loads
    // Pointers to 4 floats must be 16-byte aligned, except for the unaligned one
    VM_API vm_vec_4f_t vm_vec_4f_load ( const float* xyzw );
    VM_API vm_vec_4f_t vm_vec_4f_load_xxxx ( float x );
    VM_API vm_vec_4f_t vm_vec_4f_zero ( void );
    VM_API vm_vec_4f_t vm_vec_4f_load_reverse ( const float* wzyx );
    VM_API vm_vec_4f_t vm_vec_4f_load_x_zero_ywz ( float x );
    VM_API vm_vec_4f_t vm_vec_4f_load_unaligned ( const float* xyzw );
    VM_API vm_vec_4f_t vm_vec_4f_set ( float x, float y, float z, float w );
    VM_API vm_vec_4f_t vm_vec_4f_set_xxxx ( float x );

    // Stores
    // Pointers to 4 floats must be 16-byte aligned, except for the unaligned one
    VM_API void vm_vec_4f_store ( float* xyzw, vm_vec_4f_t vec );
    VM_API void vm_vec_4f_store_x ( float* x, vm_vec_4f_t vec );
    VM_API void vm_vec_4f_store_xxxx ( float* xxxx, vm_vec_4f_t vec );
    VM_API void vm_vec_4f_store_reverse ( float* wzyx, vm_vec_4f_t vec );
    VM_API void vm_vec_4f_store_unaligned ( float* xyzw, vm_vec_4f_t vec );

    // Math operators
    VM_API vm_vec_4f_t vm_vec_4f_add ( vm_vec_4f_t a, vm_vec_4f_t b );
    VM_API vm_vec_4f_t vm_vec_4f_add_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ );

    VM_API vm_vec_4f_t vm_vec_4f_sub ( vm_vec_4f_t a, vm_vec_4f_t b );
    VM_API vm_vec_4f_t vm_vec_4f_sub_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ );

    VM_API vm_vec_4f_t vm_vec_4f_mul ( vm_vec_4f_t a, vm_vec_4f_t b );
    VM_API vm_vec_4f_t vm_vec_4f_mul_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ );

    VM_API vm_vec_4f_t vm_vec_4f_div ( vm_vec_4f_t a, vm_vec_4f_t b );
    VM_API vm_vec_4f_t vm_vec_4f_div_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ );

    // Logical operators
    // Stores the bitwise result of the operator
    VM_API vm_vec_4f_t vm_vec_4f_and ( vm_vec_4f_t a, vm_vec_4f_t b );
    VM_API vm_vec_4f_t vm_vec_4f_not_a_and_b ( vm_vec_4f_t a, vm_vec_4f_t b );

    VM_API vm_vec_4f_t vm_vec_4f_or ( vm_vec_4f_t a, vm_vec_4f_t b );
    VM_API vm_vec_4f_t vm_vec_4f_xor ( vm_vec_4f_t a, vm_vec_4f_t b );

    // Logical comparisons
    // Vector comparisons store 0xFFFFFFFF as result for the 32 bit elements that pass and 0 for those that fail
    // Scalar comparisons just return either 1 or 0
    VM_API vm_vec_4f_t vm_vec_4f_compare_equal ( vm_vec_4f_t a, vm_vec_4f_t b );
    VM_API vm_vec_4f_t vm_vec_4f_compare_equal_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ );
    VM_API int         vm_vec_4f_compare_equal_x ( vm_vec_4f_t a, vm_vec_4f_t b );

    VM_API vm_vec_4f_t vm_vec_4f_compare_greater ( vm_vec_4f_t a, vm_vec_4f_t b );
    VM_API vm_vec_4f_t vm_vec_4f_compare_greater_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ );
    VM_API int         vm_vec_4f_compare_greater_x ( vm_vec_4f_t a, vm_vec_4f_t b );

    VM_API vm_vec_4f_t vm_vec_4f_compare_greater_equal ( vm_vec_4f_t a, vm_vec_4f_t b );
    VM_API vm_vec_4f_t vm_vec_4f_compare_greater_equal_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ );
    VM_API int         vm_vec_4f_compare_greater_equal_x ( vm_vec_4f_t a, vm_vec_4f_t b );

    VM_API vm_vec_4f_t vm_vec_4f_compare_lesser ( vm_vec_4f_t a, vm_vec_4f_t b );
    VM_API vm_vec_4f_t vm_vec_4f_compare_lesser_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ );
    VM_API int         vm_vec_4f_compare_lesser_x ( vm_vec_4f_t a, vm_vec_4f_t b );

    VM_API vm_vec_4f_t vm_vec_4f_compare_lesser_equal ( vm_vec_4f_t a, vm_vec_4f_t b );
    VM_API vm_vec_4f_t vm_vec_4f_compare_lesser_equal_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ );
    VM_API int         vm_vec_4f_compare_lesser_equal_x ( vm_vec_4f_t a, vm_vec_4f_t b );

    VM_API vm_vec_4f_t vm_vec_4f_compare_not_equal ( vm_vec_4f_t a, vm_vec_4f_t b );
    VM_API vm_vec_4f_t vm_vec_4f_compare_not_equal_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ );
    VM_API int         vm_vec_4f_compare_not_equal_x ( vm_vec_4f_t a, vm_vec_4f_t b );

    #if 0
        VM_API vm_vec_4f_t vm_vec_4f_compare_not_greater ( vm_vec_4f_t a, vm_vec_4f_t b );
        VM_API vm_vec_4f_t vm_vec_4f_compare_not_greater_x_copy_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ );

        VM_API vm_vec_4f_t vm_vec_4f_compare_not_greater_equal ( vm_vec_4f_t a, vm_vec_4f_t b );
        VM_API vm_vec_4f_t vm_vec_4f_compare_not_greater_equal_x_copy_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ );

        VM_API vm_vec_4f_t vm_vec_4f_compare_not_lesser ( vm_vec_4f_t a, vm_vec_4f_t b );
        VM_API vm_vec_4f_t vm_vec_4f_compare_not_lesser_x_copy_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ );

        VM_API vm_vec_4f_t vm_vec_4f_compare_not_lesser_equal ( vm_vec_4f_t a, vm_vec_4f_t b );
        VM_API vm_vec_4f_t vm_vec_4f_compare_not_lesser_equal_x_copy_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ );
    #endif

    VM_API vm_vec_4f_t vm_vec_4f_compare_nan_any ( vm_vec_4f_t a, vm_vec_4f_t b );
    VM_API vm_vec_4f_t vm_vec_4f_compare_nan_any_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ );

    VM_API vm_vec_4f_t vm_vec_4f_compare_not_nan_both ( vm_vec_4f_t a, vm_vec_4f_t b );
    VM_API vm_vec_4f_t vm_vec_4f_compare_not_nan_both_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ );

    // Min Max
    VM_API vm_vec_4f_t vm_vec_4f_min ( vm_vec_4f_t a, vm_vec_4f_t b );
    VM_API vm_vec_4f_t vm_vec_4f_min_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ );

    VM_API vm_vec_4f_t vm_vec_4f_max ( vm_vec_4f_t a, vm_vec_4f_t b );
    VM_API vm_vec_4f_t vm_vec_4f_max_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ );

    // Move
    VM_API vm_vec_4f_t vm_vec_4f_move_xy_xy ( vm_vec_4f_t xy__, vm_vec_4f_t zw__ );
    VM_API vm_vec_4f_t vm_vec_4f_move_zw_zw ( vm_vec_4f_t __xy, vm_vec_4f_t __zw );
    VM_API vm_vec_4f_t vm_vec_4f_move_x_yzw ( vm_vec_4f_t x___, vm_vec_4f_t _yzw );

    // Mask
    // Sets each of the first 4 bits in the result to the value of the most significant bit of the corresponding vector element
    VM_API uint8_t vm_vec_4f_mask ( vm_vec_4f_t vec );

    // Shuffle
    // Each element in the result takes the value of the element in the source that's indexed by the corresponding value in the mask
    // The source vector is A for result.xy and B for result.zw. Result = <a[x], a[y], b[z], b[w]>
    //#define VM_VEC_4F_SHUFFLE_MASK(x, y, z, w) ( x | ( y << 2 ) | ( z << 4 ) | ( w << 6 ) )
    #define vm_vec_4f_shuffle_m( a, b, x, y, z, w ) _mm_shuffle_ps ( a, b, _MM_SHUFFLE ( x, y, z, w ) )

#endif

#include "vm_vector.inl"
