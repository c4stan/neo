#pragma once

#include <sm_types.h>

// ======================================================================================= //
//                                  C O N S T R U C T O R
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

// ======================================================================================= //
//                                    T R A N S P O S E
// ======================================================================================= //
/* template begin

def <TYPE, PREFIX, ROWS, COLS, SIZE>
sm_mat_$ROWSx$COLS$PREFIX_t sm_matrix_$ROWSx$COLS$PREFIX_transpose ( sm_mat_$ROWSx$COLS$PREFIX_t mat );

make <float, f, 3, 3>
make <float, f, 4, 4>
*/
// template generation begin
sm_mat_3x3f_t sm_matrix_3x3f_transpose ( sm_mat_3x3f_t mat );
sm_mat_4x4f_t sm_matrix_4x4f_transpose ( sm_mat_4x4f_t mat );
// template generation end

// ======================================================================================= //
//                                   P R O J E C T I O N
// ======================================================================================= //
sm_mat_4x4f_t sm_matrix_view ( sm_vec_3f_t position, sm_quat_t orientation );

typedef struct {
    float aspect_ratio;
    float near_z;
    float far_z;
    // TODO use fov_x instead?
    float fov_y; // in radians
    bool reverse_z;
    bool infinite_far_z;
} sm_perspective_projection_params_t;

#define sm_perspective_projection_params_m( ... ) ( sm_perspective_projection_params_t ) { \
    .aspect_ratio = ( 16.f / 9.f ), \
    .near_z = 0.1f, \
    .far_z = 1000.f, \
    .fov_y = 90 * sm_deg_to_rad_m, \
    .reverse_z = false, \
    .infinite_far_z = false, \
    __VA_ARGS__ \
}

sm_mat_4x4f_t sm_matrix_perspective_proj ( const sm_perspective_projection_params_t* params );
