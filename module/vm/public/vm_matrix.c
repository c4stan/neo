//#include <vm_matrix.h>

#if 0
void vm_matrix_4x4f_transform_f3 ( float* restrict result, float* restrict mat, float* restrict vec ) {
    sm_vec_3f_dot ( result + 0, mat + 0, vec );
    sm_vec_3f_dot ( result + 1, mat + 4, vec );
    sm_vec_3f_dot ( result + 2, mat + 8, vec );
}

void vm_matrix_4x4f_axis_rotation ( float* restrict result, const float* restrict axis, float radians ) {
    float c = cosf ( radians );
    float s = sinf ( radians );

    //float norm_axis[3];
    //sm_vec_3f_norm ( norm_axis, axis );

    float temp[3];
    temp[0] = ( 1.f - c ) * axis[0];
    temp[1] = ( 1.f - c ) * axis[1];
    temp[2] = ( 1.f - c ) * axis[2];

    result[0] = c + temp[0] * axis[0];
    result[1] = temp[0] * axis[1] + s * axis[2];
    result[2] = temp[0] * axis[2] - s * axis[1];
    result[3] = 0;

    result[4] = temp[1] * axis[0] - s * axis[2];
    result[5] = c + temp[1] * axis[1];
    result[6] = temp[1] * axis[2] + s * axis[0];
    result[7] = 0;

    result[8] = temp[2] * axis[0] + s * axis[1];
    result[9] = temp[2] * axis[1] - s * axis[0];
    result[10] = c + temp[2] * axis[2];
    result[11] = 0;

    result[12] = 0;
    result[13] = 0;
    result[14] = 0;
    result[15] = 1;
}
#endif
