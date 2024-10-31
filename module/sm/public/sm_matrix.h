#pragma once

#include <sm_vector.h>

// ======================================================================================= //
//                                       M A T R I X
// ======================================================================================= //

// NxM matrix: N rows, M columns
// Row major storage, e.g. for a 4x4 matrix:
// r0 = m[0][] = e00 e01 e02 e03
// r1 = m[1][] = e04 e05 e06 e07
// r2 = m[2][] = e08 e09 e10 e11
// r3 = m[3][] = e12 e13 e14 e15

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
        $FOR $ROWS
        $TYPE r$i[$COLS];
        $END_FOR
    };
    struct {
        $FOR $ROWS
        sm_vec_$COLS$PREFIX_t v$i;
        $END_FOR
    };
} sm_mat_$ROWSx$COLS$PREFIX_t;

make <float, f, 4, 4>

*/
// template generation begin
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
//                                   C O N T R U C T O R
// ======================================================================================= //
sm_mat_4x4f_t sm_matrix_4x4f ( const float f[16] );

// ======================================================================================= //
//                                    T R A N S F O R M
// ======================================================================================= //
// TODO name sm_mat_... instead of sm_matrix_... ?
/* template begin

def <TYPE, PREFIX, ROWS, COLS, SIZE>
sm_vec_$SIZE$PREFIX_t sm_matrix_$ROWSx$COLS$PREFIX_transform_$PREFIX$SIZE ( sm_mat_$ROWSx$COLS$PREFIX_t mat, sm_vec_$SIZE$PREFIX_t vec );

make <float, f, 4, 4, 3>
make <float, f, 4, 4, 4>

*/
// template generation begin
sm_vec_3f_t sm_matrix_4x4f_transform_f3 ( sm_mat_4x4f_t mat, sm_vec_3f_t vec );
sm_vec_4f_t sm_matrix_4x4f_transform_f4 ( sm_mat_4x4f_t mat, sm_vec_4f_t vec );
// template generation end

// axis is assumed to be normalized!
sm_mat_4x4f_t sm_matrix_4x4f_axis_rotation ( sm_vec_3f_t axis, float radians );

sm_mat_4x4f_t sm_matrix_4x4f_dir_rotation ( sm_vec_3f_t dir, sm_vec_3f_t up );

sm_mat_4x4f_t sm_matrix_4x4f_mul ( sm_mat_4x4f_t a, sm_mat_4x4f_t b );

sm_vec_3f_t sm_matrix_4x4f_transform_f3_dir ( sm_mat_4x4f_t mat, sm_vec_3f_t vec );

// ======================================================================================= //
//                                      I N V E R S E
// ======================================================================================= //
/* template begin

def <TYPE, PREFIX, ROWS, COLS>
sm_matrix_$ROWSx$COLS$PREFIX_t sm_matrix_$ROWSx$COLS$PREFIX_inverse ( sm_mat_$ROWSx$COLS$PREFIX_t mat );


*/
// TODO