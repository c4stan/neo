#include <sm_matrix.h>

#include <std_byte.h>

#include <math.h>

sm_mat_4x4f_t sm_matrix_4x4f ( const float f[16] ) {
    sm_mat_4x4f_t result = {
        f[0],  f[1],  f[2],  f[3],
        f[4],  f[5],  f[6],  f[7],
        f[8],  f[9],  f[10], f[11],
        f[12], f[13], f[14], f[15],
    };
    return result;
}

sm_vec_4f_t sm_matrix_4x4f_transform_f4 ( sm_mat_4x4f_t mat, sm_vec_4f_t vec ) {
    sm_vec_4f_t result = {
        sm_vec_4f_dot ( mat.v0, vec ),
        sm_vec_4f_dot ( mat.v1, vec ),
        sm_vec_4f_dot ( mat.v2, vec ),
        sm_vec_4f_dot ( mat.v3, vec ),
    };
    return result;
}

sm_vec_3f_t sm_matrix_4x4f_transform_f3 ( sm_mat_4x4f_t mat, sm_vec_3f_t vec ) {
    sm_vec_4f_t vec4 = { vec.x, vec.y, vec.z, 1 };
    vec4 = sm_matrix_4x4f_transform_f4 ( mat, vec4 );
    sm_vec_3f_t result = { vec4.x, vec4.y, vec4.z };
    return result;
}

sm_vec_3f_t sm_matrix_4x4f_transform_f3_dir ( sm_mat_4x4f_t mat, sm_vec_3f_t vec ) {
    sm_vec_3f_t result;
    result.x = sm_vec_3f_dot ( sm_vec_4f_to_3f ( mat.v0 ), vec );
    result.y = sm_vec_3f_dot ( sm_vec_4f_to_3f ( mat.v1 ), vec );
    result.z = sm_vec_3f_dot ( sm_vec_4f_to_3f ( mat.v2 ), vec );
    return result;
}

sm_mat_4x4f_t sm_matrix_4x4f_axis_rotation ( sm_vec_3f_t axis, float radians ) {
    float c = cosf ( radians );
    float s = sinf ( radians );

    sm_vec_3f_t temp;
    temp.x = ( 1.f - c ) * axis.x;
    temp.y = ( 1.f - c ) * axis.y;
    temp.z = ( 1.f - c ) * axis.z;

    sm_mat_4x4f_t result;
    result.v0.x = c + temp.x * axis.x;
    result.v0.y = temp.x * axis.y - s * axis.z;
    result.v0.z = temp.x * axis.z + s * axis.y;
    result.v0.w = 0;

    result.v1.x = temp.y * axis.x + s * axis.z;
    result.v1.y = c + temp.y * axis.y;
    result.v1.z = temp.y * axis.z - s * axis.x;
    result.v1.w = 0;

    result.v2.x = temp.z * axis.x - s * axis.y;
    result.v2.y = temp.z * axis.y + s * axis.x;
    result.v2.z = c + temp.z * axis.z;
    result.v2.w = 0;

    result.v3.x = 0;
    result.v3.y = 0;
    result.v3.z = 0;
    result.v3.w = 1;

    return result;
}

sm_mat_4x4f_t sm_matrix_4x4f_dir_rotation ( sm_vec_3f_t dir, sm_vec_3f_t up ) {
    sm_vec_3f_t x_axis = sm_vec_3f_cross ( up, dir );
    x_axis = sm_vec_3f_norm ( x_axis );

    sm_vec_3f_t y_axis = sm_vec_3f_cross ( dir, x_axis );
    y_axis = sm_vec_3f_norm ( y_axis );

    sm_mat_4x4f_t result;
    result.v0.x = x_axis.x;
    result.v0.y = y_axis.x;
    result.v0.z = dir.x;
    result.v0.w = 0;

    result.v1.x = x_axis.y;
    result.v1.y = y_axis.y;
    result.v1.z = dir.y;
    result.v1.w = 0;

    result.v2.x = x_axis.z;
    result.v2.y = y_axis.z;
    result.v2.z = dir.z;
    result.v2.w = 0;

    result.v3.x = 0;
    result.v3.y = 0;
    result.v3.z = 0;
    result.v3.w = 1;

    return result;
}

sm_mat_4x4f_t sm_matrix_4x4f_mul ( sm_mat_4x4f_t a, sm_mat_4x4f_t b ) {
    sm_mat_4x4f_t result;

    for ( int i = 0; i < 4; ++i ) {
        for ( int j = 0; j < 4; ++j ) {
            result.e[i * 4 + j] = 0;

            for ( int k = 0; k < 4; ++k ) {
                result.e[i * 4 + j] += a.e[i * 4 + k] * b.e[k * 4 + j];
            }
        }
    }

    return result;
}
