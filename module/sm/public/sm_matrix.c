#include <sm_matrix.h>

#include <sm_vector.h>
#include <sm_quat.h>

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
    sm_vec_3f_t z_axis = sm_vec_3f_norm ( dir );
    sm_vec_3f_t x_axis = sm_vec_3f_norm ( sm_vec_3f_cross ( up, z_axis ) );
    sm_vec_3f_t y_axis = sm_vec_3f_norm ( sm_vec_3f_cross ( z_axis, x_axis ) );

    sm_mat_4x4f_t result;
    result.v0.x = x_axis.x;
    result.v0.y = y_axis.x;
    result.v0.z = z_axis.x;
    result.v0.w = 0;

    result.v1.x = x_axis.y;
    result.v1.y = y_axis.y;
    result.v1.z = z_axis.y;
    result.v1.w = 0;

    result.v2.x = x_axis.z;
    result.v2.y = y_axis.z;
    result.v2.z = z_axis.z;
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

// ======================================================================================= //
//                                    T R A N S P O S E
// ======================================================================================= //
/* template begin

def <TYPE, PREFIX, ROWS, COLS, SIZE>
sm_mat_$ROWSx$COLS$PREFIX_t sm_matrix_$ROWSx$COLS$PREFIX_transpose ( sm_mat_$ROWSx$COLS$PREFIX_t mat ) {
    sm_mat_$ROWSx$COLS$PREFIX_t result;
    for ( uint32_t i = 0; i < $ROWS; ++i ) {
        for ( uint32_t j = 0; j < $COLS; ++j ) {
            result.m[i][j] = mat.m[j][i];
        }
    }
    return result;
}

make <float, f, 3, 3>
make <float, f, 4, 4>
*/
// template generation begin
sm_mat_3x3f_t sm_matrix_3x3f_transpose ( sm_mat_3x3f_t mat ) {
    sm_mat_3x3f_t result;
    for ( uint32_t i = 0; i < 3; ++i ) {
        for ( uint32_t j = 0; j < 3; ++j ) {
            result.m[i][j] = mat.m[j][i];
        }
    }
    return result;
}
sm_mat_4x4f_t sm_matrix_4x4f_transpose ( sm_mat_4x4f_t mat ) {
    sm_mat_4x4f_t result;
    for ( uint32_t i = 0; i < 4; ++i ) {
        for ( uint32_t j = 0; j < 4; ++j ) {
            result.m[i][j] = mat.m[j][i];
        }
    }
    return result;
}
// template generation end

sm_mat_4x4f_t sm_matrix_view ( sm_vec_3f_t position, sm_quat_t orientation ) {
    /*
        https://www.3dgep.com/understanding-the-view-matrix/
        view = orientation^-1 * translation^-1
    */

    sm_quat_t inverse_orientation = sm_quat_inverse ( orientation );
    sm_mat_4x4f_t result = sm_quat_to_4x4f ( inverse_orientation );

    result.v0.w = -sm_vec_3f_dot ( sm_vec_4f_to_3f ( result.v0 ), position );
    result.v1.w = -sm_vec_3f_dot ( sm_vec_4f_to_3f ( result.v1 ), position );
    result.v2.w = -sm_vec_3f_dot ( sm_vec_4f_to_3f ( result.v2 ), position );
    result.v3.w = 1;

    return result;
}

sm_mat_4x4f_t sm_matrix_view_lookat ( sm_vec_3f_t position, sm_vec_3f_t dir ) {
    // Build orthonormal basis for lookat, then build view matrix as usual
    sm_mat_4x4f_t result = { 0 };

    dir = sm_vec_3f_norm ( dir );
    sm_vec_3f_t right = sm_vec_3f_norm ( sm_vec_3f_cross ( dir, sm_vec_3f_set ( 0, 1, 0 ) ) );
    sm_vec_3f_t up = sm_vec_3f_cross ( right, dir );

    sm_vec_3f_t z_axis = dir;
    if ( z_axis.e[0] == 0 && ( z_axis.e[1] == -1 || z_axis.e[1] == 1 ) && z_axis.e[2] == 0 ) {
        up.e[0] = 0;
        up.e[1] = 0;
        up.e[2] = 1;
    }
    sm_vec_3f_t x_axis = sm_vec_3f_norm ( sm_vec_3f_cross ( up, z_axis ) );
    sm_vec_3f_t y_axis = sm_vec_3f_norm ( sm_vec_3f_cross ( z_axis, x_axis ) );

    result.v0 = sm_vec_3f_to_4f ( x_axis, -sm_vec_3f_dot ( x_axis, position ) );
    result.v1 = sm_vec_3f_to_4f ( y_axis, -sm_vec_3f_dot ( y_axis, position ) );
    result.v2 = sm_vec_3f_to_4f ( z_axis, -sm_vec_3f_dot ( z_axis, position ) );
    result.v3.w = 1;

    return result;
}

sm_mat_4x4f_t sm_matrix_orthographic_proj ( const sm_orthographic_projection_params_t* params ) {
    sm_mat_4x4f_t result = { 0 };

    result.r0[0] = 2.0f / ( params->right - params->left );
    result.r0[3] = - ( params->right + params->left ) / ( params->right - params->left );

    result.r1[1] = 2.0f / ( params->top - params->bottom );
    result.r1[3] = - ( params->top + params->bottom ) / ( params->top - params->bottom );

    result.r2[2] = -2.0f / ( params->far_z - params->near_z );
    result.r2[3] = - ( params->far_z + params->near_z ) / ( params->far_z - params->near_z );

    result.r3[3] = 1;

    return result;
}

sm_mat_4x4f_t sm_matrix_perspective_proj ( const sm_perspective_projection_params_t* params ) {
    /*
        http://www.songho.ca/opengl/gl_projectionmatrix.html
        https://matthewwellings.com/blog/the-new-vulkan-coordinate-system/
        Premise:
            Eye space is left handed. (Camera towards positive Z axis)
    -- X, Y --
        eye -> near plane (xe,ye -> xp,yp)
            <x,y,z>e somewhere inside the frustum
            xp in [-w/2, w/2], yp in  [-h/2, h/2]
            [from similar triangles formula: xp/xe == znear/ze]
            xp = znear * xe / ze
            yp = znear * ye / ze
            *division by ze happens automatically in hw later*
        near plane -> NDC (xp, yp -> xn, yn)
            xp in [-w/2, w/2], yp in  [-h/2, h/2]
            xn in [-1, 1], yn in [-1, 1]
            [simple remapping]
            xn = xp / w * 2
            yn = yp / h * 2
        putting it together, eye -> NDC
            xn = 2 * znear / w * xe = znear / (w/2) * xe
            yn = 2 * znear / h * ye = znear / (h/2) * ye
    -- Z --
        eye -> NDC (ze -> zn)
            ze in [znear, zfar]
            zn in [0, 1]
            zn = (A*ze + B) / ze ( = A + B / ze )
            [the target remapping rewritten as system]
            | (A*znear + B) / znear = 0
            | (A*zfar + B) / zfar = 1
            [solving for A, B]
            B = -A*znear -> (A*zfar - A*znear) / zfar = 1 -> A = zfar / (zfar - znear)
            B = -(zfar * znear) / (zfar - znear)
        for reversed Z:
            | (A*znear + B) / znear = 1
            | (A*zfar + B) / zfar = 0
            [solving for A, B]
            B = -A*zfar -> (A*znear - A*zfar) / znear = 1 -> A = znear / (znear - zfar)
            B = -(zfar * znear) / (znear - zfar)
    */

    sm_mat_4x4f_t result = { 0 };

    float near_z = params->near_z;
    float far_z = params->far_z;
    float half_h = near_z * tanf ( params->fov_y / 2.f );
    float half_w = params->aspect_ratio * half_h;

    result.r0[0] = near_z / half_w;
    result.r1[1] = near_z / half_h;

    if ( params->reverse_z ) {
        if ( params->infinite_far_z ) {
            result.r2[2] = 1e-6;
            result.r2[3] = near_z * ( 1 - 1e-6 );
        } else {
            result.r2[2] = near_z / ( near_z - far_z );
            result.r2[3] = - ( far_z * near_z ) / ( near_z - far_z );        
        }
    } else {
        if ( params->infinite_far_z ) {
            result.r2[2] = 1 - 1e-6;
            result.r2[3] = -near_z * ( 1 - 1e-6 );
        } else {
            result.r2[2] = far_z / ( far_z - near_z );
            result.r2[3] = - ( far_z * near_z ) / ( far_z - near_z );
        }
    }

    result.r3[2] = 1;

    return result;
}
