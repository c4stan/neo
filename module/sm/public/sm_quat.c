#include <sm_quat.h>

#include <sm_vector.h>

#include <math.h>

sm_quat_t sm_quat ( const float f[4] ) {
    sm_quat_t quat = { f[0], f[1], f[2], f[3] };
    return quat;
}

sm_quat_t sm_quat_identity( void ) {
    sm_quat_t id = { .e = { 0, 0, 0, 1 } };
    return id;
}

sm_quat_t sm_quat_mul ( sm_quat_t q1, sm_quat_t q2 ) {
    sm_quat_t q;
    q.e[0] = q1.e[0] * q2.e[3] + q1.e[1] * q2.e[2] - q1.e[2] * q2.e[1] + q1.e[3] * q2.e[0];
    q.e[1] = q1.e[1] * q2.e[3] + q1.e[2] * q2.e[0] + q1.e[3] * q2.e[1] - q1.e[0] * q2.e[2];
    q.e[2] = q1.e[2] * q2.e[3] + q1.e[3] * q2.e[2] + q1.e[0] * q2.e[1] - q1.e[1] * q2.e[0];
    q.e[3] = q1.e[3] * q2.e[3] - q1.e[0] * q2.e[0] - q1.e[1] * q2.e[1] - q1.e[2] * q2.e[2];
    return q;
}

sm_quat_t sm_quat_conj ( sm_quat_t q ) {
    sm_quat_t c;
    c.e[0] = -q.e[0];
    c.e[1] = -q.e[1];
    c.e[2] = -q.e[2];
    c.e[3] =  q.e[3];
    return c;
}

sm_quat_t sm_quat_inverse ( sm_quat_t q ) {
    float len_sq = sm_vec_4f_dot ( q.vec4, q.vec4 );
    sm_quat_t inv = sm_quat_conj ( q );
    inv.vec4 = sm_vec_4f_mul ( inv.vec4, 1.f / len_sq );
    return inv;
}

sm_vec_3f_t sm_quat_transform_f3 ( sm_quat_t q, sm_vec_3f_t vec ) {
    float q2 = sm_vec_3f_dot ( q.vec, q.vec );
    sm_vec_3f_t a = sm_vec_3f_mul ( vec, q.w * q.w - q2 );
    sm_vec_3f_t b = sm_vec_3f_mul ( q.vec, sm_vec_3f_dot ( vec, q.vec ) * 2.f );
    sm_vec_3f_t c = sm_vec_3f_mul ( sm_vec_3f_cross ( q.vec, vec ), q.w * 2.f );
    return sm_vec_3f_add ( sm_vec_3f_add ( a, b ), c );
}

sm_quat_t sm_quat_axis_rotation ( sm_vec_3f_t axis, float radians ) {
    float half_angle = radians * 0.5f;
    float sin_half = sinf ( half_angle );
    float cos_half = cosf ( half_angle );

    sm_quat_t q = {
        .x = axis.x * sin_half,
        .y = axis.y * sin_half,
        .z = axis.z * sin_half,
        .w = cos_half,
    };
    return q;
}

sm_mat_4x4f_t sm_quat_to_4x4f ( sm_quat_t q ) {
    float x2 = q.x * q.x;
    float y2 = q.y * q.y;
    float z2 = q.z * q.z;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;
    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;

    sm_mat_4x4f_t m;
    
    m.e[0] = 1.f - 2.f * ( y2 + z2 );
    m.e[1] = 2.f * ( xy - wz );
    m.e[2] = 2.f * ( xz + wy );
    m.e[3] = 0;

    m.e[4] = 2.f * ( xy + wz );
    m.e[5] = 1.f - 2.f * ( x2 + z2 );
    m.e[6] = 2.f * ( yz - wx );
    m.e[7] = 0;

    m.e[8] = 2.f * ( xz - wy );
    m.e[9] = 2.f * ( yz + wx );
    m.e[10] = 1.f - 2.f * ( x2 + y2 );
    m.e[11] = 0;

    m.e[12] = 0;
    m.e[13] = 0;
    m.e[14] = 0;
    m.e[15] = 1;

    return m;
}

sm_quat_t sm_quat_from_4x4f ( sm_mat_4x4f_t m ) {
    sm_quat_t quat;

    float m00 = m.v0.x;
    float m01 = m.v0.y;
    float m02 = m.v0.z;
    float m10 = m.v1.x;
    float m11 = m.v1.y;
    float m12 = m.v1.z;
    float m20 = m.v2.x;
    float m21 = m.v2.y;
    float m22 = m.v2.z;
    float sum = m00 + m11 + m22;

    if ( sum > 0.f ) {
        float s = sqrtf ( sum + 1.f ) * 2.f;
        quat.w = 0.25f * s;
        quat.x = ( m21 - m12 ) / s;
        quat.y = ( m02 - m20 ) / s;
        quat.z = ( m10 - m01 ) / s;
    } else if ( m00 > m11 && m00 > m22 ) {
        float s = sqrtf ( 1.f + m00 - m11 - m22 ) * 2.f;
        quat.w = ( m21 - m12 ) / s;
        quat.x = 0.25f * s;
        quat.y = ( m01 + m10 ) / s;
        quat.z = ( m02 + m20 ) / s;
    } else if ( m11 > m22 ) {
        float s = sqrtf ( 1.f + m11 - m00 - m22 ) * 2.f;
        quat.w = ( m02 - m20 ) / s;
        quat.x = ( m01 + m10 ) / s;
        quat.y = 0.25f * s;
        quat.z = ( m12 + m21 ) / s;
    } else {
        float s = sqrtf ( 1.f + m22 - m00 - m11 ) * 2.f;
        quat.w = ( m10 - m01 ) / s;
        quat.x = ( m02 + m20 ) / s;
        quat.y = ( m12 + m21 ) / s;
        quat.z = 0.25f * s;
    }

    return quat;
}

sm_quat_t sm_quat_from_vec ( sm_vec_3f_t dir ) {
    sm_quat_t quat;

    // Reference direction (z-axis)
    sm_vec_3f_t ref = { 0.0f, 0.0f, 1.0f };
    
    // Compute dot product
    float dot = ref.x * dir.x + ref.y * dir.y + ref.z * dir.z;
    
    // Handle case where vectors are parallel or anti-parallel
    if ( fabs ( dot ) > 0.99999f) {
        if ( dot > 0 ) {
            // Identity quaternion (no rotation)
            quat = sm_quat_identity();
        } else {
            // 180-degree rotation around x-axis
            quat.w = 0.0f;
            quat.x = 1.0f;
            quat.y = 0.0f;
            quat.z = 0.0f;
        }
        return quat;
    }
    
    // Compute rotation axis (cross product)
    sm_vec_3f_t axis = {
        ref.y * dir.z - ref.z * dir.y,
        ref.z * dir.x - ref.x * dir.z,
        ref.x * dir.y - ref.y * dir.x
    };
    
    // Normalize axis
    float len = sqrtf ( axis.x * axis.x + axis.y * axis.y + axis.z * axis.z );
    axis.x /= len;
    axis.y /= len;
    axis.z /= len;
    
    // Compute rotation angle
    float angle = acosf ( dot );
    
    // Convert axis-angle to quaternion
    float half_angle = angle * 0.5f;
    float s = sinf ( half_angle );
    quat.w = cosf ( half_angle );
    quat.x = axis.x * s;
    quat.y = axis.y * s;
    quat.z = axis.z * s;

    return quat;
}

sm_vec_3f_t sm_quat_to_vec ( sm_quat_t quat ) {
    // Reference direction (e.g., z-axis)
    sm_vec_3f_t ref = { 0.0f, 0.0f, 1.0f };
    return sm_quat_transform_f3 ( quat, ref );
}
