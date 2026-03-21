#pragma once

#include <std_platform.h>

#define sm_deg_to_rad_m 0.0174533f
#define sm_rad_to_deg_m 57.2958f
#define sm_rad_quad_m ( 90 * sm_deg_to_rad_m )
#define sm_rad_semi_m ( 180 * sm_deg_to_rad_m )

// ======================================================================================= //
//                                       V E C T O R
// ======================================================================================= //
/* template begin

def <TYPE, PREFIX, SIZE>
typedef union {
    float e[$SIZE];
    struct {
        $TYPE x;
$IF $SIZE > 1
        $TYPE y;
$END_IF
$IF $SIZE > 2
        $TYPE z;
$END_IF
$IF $SIZE > 3
        $TYPE w;
$END_IF
    };
} sm_vec_$SIZE$PREFIX_t;

#define sm_vec_$SIZE$PREFIX_log_m( v ) std_log_info_m ( \
    std_fmt_f32_m \
    $FOR 1 $SIZE
    " " std_fmt_f32_m \
    $END_FOR
    , \
    v.e[0] \
    $FOR 1 $SIZE
    , v.e[$i] \
    $END_FOR
)

make <float, f, 3>

make <float, f, 4>

*/
// template generation begin
typedef union {
    float e[3];
    struct {
        float x;
        float y;
        float z;
    };
} sm_vec_3f_t;

#define sm_vec_3f_log_m( v ) std_log_info_m ( \
    std_fmt_f32_m \
    " " std_fmt_f32_m \
    " " std_fmt_f32_m \
    , \
    v.e[0] \
    , v.e[1] \
    , v.e[2] \
)

typedef union {
    float e[4];
    struct {
        float x;
        float y;
        float z;
        float w;
    };
} sm_vec_4f_t;

#define sm_vec_4f_log_m( v ) std_log_info_m ( \
    std_fmt_f32_m \
    " " std_fmt_f32_m \
    " " std_fmt_f32_m \
    " " std_fmt_f32_m \
    , \
    v.e[0] \
    , v.e[1] \
    , v.e[2] \
    , v.e[3] \
)
// template generation end

// ======================================================================================= //
//                                       M A T R I X
// ======================================================================================= //

// NxM matrix: N rows, M columns
// Row major storage, e.g. for a 4x4 matrix:
// r0 = m[0][] = e00 e01 e02 e03
// r1 = m[1][] = e04 e05 e06 e07
// r2 = m[2][] = e08 e09 e10 e11
// r3 = m[3][] = e12 e13 e14 e15
// m[i][j] -> i = row, j = column

// Vectors can be considered as Nx1 column matrices
// Matrix vector product follows the M*v convention
// A 4x4 affine transform matrix has the following layout
//      |     R   | T |
// r0 = | Xx Yx Zx Tx |
// r1 = | Xy Yy Zy Ty |
// r2 = | Xz Yz Zz Tz |
// r3 = | 0  0  0  1  |

/* template begin

def <TYPE, PREFIX, ROWS, COLS>
typedef union {
    $TYPE e[$ROWS * $COLS];
    $TYPE m[$ROWS][$COLS];
    struct {
        $FOR 0 $ROWS
        $TYPE r$i[$COLS];
        $END_FOR
    };
    struct {
        $FOR 0 $ROWS
        sm_vec_$COLS$PREFIX_t v$i;
        $END_FOR
    };
} sm_mat_$ROWSx$COLS$PREFIX_t;

make <float, f, 3, 3>

make <float, f, 4, 4>
*/
// template generation begin
typedef union {
    float e[3 * 3];
    float m[3][3];
    struct {
        float r0[3];
        float r1[3];
        float r2[3];
    };
    struct {
        sm_vec_3f_t v0;
        sm_vec_3f_t v1;
        sm_vec_3f_t v2;
    };
} sm_mat_3x3f_t;

typedef union {
    float e[4 * 4];
    float m[4][4];
    struct {
        float r0[4];
        float r1[4];
        float r2[4];
        float r3[4];
    };
    struct {
        sm_vec_4f_t v0;
        sm_vec_4f_t v1;
        sm_vec_4f_t v2;
        sm_vec_4f_t v3;
    };
} sm_mat_4x4f_t;
// template generation end

// ======================================================================================= //
//                                   Q U A T E R N I O N
// ======================================================================================= //
typedef union {
    float e[4];
    struct {
        float x;
        float y;
        float z;
        float w;
    };
    struct {
        sm_vec_3f_t vec;
        float scalar;
    };
    sm_vec_4f_t vec4;
} sm_quat_t;

