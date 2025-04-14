#pragma once

#include <sm_matrix.h>

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
} sm_quat_t;

sm_quat_t sm_quat ( const float f[4] );
sm_quat_t sm_quat_identity ( void );

sm_quat_t sm_quat_mul ( sm_quat_t a, sm_quat_t b );
sm_quat_t sm_quat_conj ( sm_quat_t q );
sm_vec_3f_t sm_quat_transform_f3 ( sm_quat_t q, sm_vec_3f_t vec );

sm_mat_4x4f_t sm_quat_to_4x4f ( sm_quat_t q );
sm_quat_t sm_quat_from_4x4f ( sm_mat_4x4f_t m );

// uses +z as "base" direction
sm_quat_t sm_quat_from_vec ( sm_vec_3f_t dir );
sm_vec_3f_t sm_quat_to_vec ( sm_quat_t quat );
