#include <sm_quat.h>

#include <math.h>

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

sm_vec_3f_t sm_quat_transform_f3 ( sm_quat_t q, sm_vec_3f_t vec ) {
    float q2 = sm_vec_3f_dot ( q.vec, q.vec );
    sm_vec_3f_t a = sm_vec_3f_mul ( vec, q.w * q.w - q2 );
    sm_vec_3f_t b = sm_vec_3f_mul ( q.vec, sm_vec_3f_dot ( vec, q.vec ) * 2.f );
    sm_vec_3f_t c = sm_vec_3f_mul ( sm_vec_3f_cross ( q.vec, vec ), q.w * 2.f );
    return sm_vec_3f_add ( sm_vec_3f_add ( a, b ), c );
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
    sm_quat_t q;

    if ( m.m[0][0] + m.m[1][1] + m.m[2][2] > 0.f ) {
        q.w = sqrtf ( m.m[0][0] + m.m[1][1] + m.m[2][2] + 1.f ) * 0.5f;
        float f = 0.25f / q.w;
        q.x = ( m.m[2][1] - m.m[1][2] ) * f;
        q.y = ( m.m[0][2] - m.m[2][0] ) * f;
        q.z = ( m.m[1][0] - m.m[0][1] ) * f;
    } else if ( m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2] ) {
        q.x = sqrtf ( m.m[0][0] - m.m[1][1] - m.m[2][2] + 1.f ) * 0.5f;
        float f = 0.25f / q.x;
        q.y = ( m.m[1][0] - m.m[0][1] ) * f;
        q.z = ( m.m[0][2] - m.m[2][0] ) * f;
        q.w = ( m.m[2][1] - m.m[1][2] ) * f;
    } else if ( m.m[1][1] > m.m[2][2] ) {
        q.y = sqrtf ( m.m[1][1] - m.m[0][0] - m.m[2][2] + 1.f ) * 0.5f;
        float f = 0.25f / q.y;
        q.x = ( m.m[1][0] - m.m[0][1] ) * f;
        q.z = ( m.m[2][1] - m.m[1][1] ) * f;
        q.w = ( m.m[0][2] - m.m[2][1] ) * f;
    } else {
        q.z = sqrtf ( m.m[2][2] - m.m[0][0] - m.m[1][1] + 1.f ) * 0.5f;
        float f = 0.25f * q.z;
        q.x = ( m.m[0][2] - m.m[2][0] ) * f;
        q.y = ( m.m[2][1] - m.m[1][2] ) * f;
        q.w = ( m.m[1][0] - m.m[0][1] ) * f;
    }

    return q;
}
