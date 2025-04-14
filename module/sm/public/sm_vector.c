#include <sm_vector.h>

#include <math.h>

sm_vec_3f_t sm_vec_3f ( const float f[3] ) {
    sm_vec_3f_t vec = { f[0], f[1], f[2] };
    return vec;
}

sm_vec_4f_t sm_vec_4f ( const float f[4] ) {
    sm_vec_4f_t vec = { f[0], f[1], f[2], f[3] };
    return vec;
}

sm_vec_3f_t sm_vec_3f_set ( float x, float y, float z ) {
    sm_vec_3f_t vec = { x, y, z };
    return vec;
}

sm_vec_4f_t sm_vec_4f_set ( float x, float y, float z, float w ) {
    sm_vec_4f_t vec = { x, y, z, w };
    return vec;
}

sm_vec_3f_t sm_vec_4f_to_3f ( sm_vec_4f_t vec ) {
    sm_vec_3f_t result;
    result.x = vec.x;
    result.y = vec.y;
    result.z = vec.z;
    return result;
}

sm_vec_4f_t sm_vec_3f_to_4f ( sm_vec_3f_t vec, float w ) {
    sm_vec_4f_t result;
    result.x = vec.x;
    result.y = vec.y;
    result.z = vec.z;
    result.w = w;
    return result;
}

float sm_vec_3f_len ( sm_vec_3f_t vec ) {
    return sqrtf ( vec.x * vec.x + vec.y * vec.y + vec.z * vec.z );
}

sm_vec_3f_t sm_vec_3f_norm ( sm_vec_3f_t vec ) {
    float len = sm_vec_3f_len ( vec );
    sm_vec_3f_t result;
    result.x = vec.x / len;
    result.y = vec.y / len;
    result.z = vec.z / len;
    return result;
}

float sm_vec_3f_dot ( sm_vec_3f_t a, sm_vec_3f_t b ) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float sm_vec_4f_dot ( sm_vec_4f_t a, sm_vec_4f_t b ) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

sm_vec_3f_t sm_vec_3f_cross ( sm_vec_3f_t a, sm_vec_3f_t b ) {
    sm_vec_3f_t result;
    result.x = a.y * b.z - a.z * b.y;
    result.y = a.z * b.x - a.x * b.z;
    result.z = a.x * b.y - a.y * b.x;
    return result;
}

sm_vec_3f_t sm_vec_3f_mul ( sm_vec_3f_t vec, float scale ) {
    sm_vec_3f_t result;
    result.x = vec.x * scale;
    result.y = vec.y * scale;
    result.z = vec.z * scale;
    return result;
}

sm_vec_3f_t sm_vec_3f_add ( sm_vec_3f_t a, sm_vec_3f_t b ) {
    sm_vec_3f_t result;
    result.x = a.x + b.x;
    result.y = a.y + b.y;
    result.z = a.z + b.z;
    return result;
}

sm_vec_3f_t sm_vec_3f_sub ( sm_vec_3f_t a, sm_vec_3f_t b ) {
    sm_vec_3f_t result;
    result.x = a.x - b.x;
    result.y = a.y - b.y;
    result.z = a.z - b.z;
    return result;
}

sm_vec_3f_t sm_vec_3f_neg ( sm_vec_3f_t a ) {
    return ( sm_vec_3f_t ) { -a.x, -a.y, -a.z };
}
