#include <sm_vector.h>

#include <math.h>

sm_vec_3f_t sm_vec_3f ( float x, float y, float z ) {
    sm_vec_3f_t vec;
    vec.x = x;
    vec.y = y;
    vec.z = z;
    return vec;
}

sm_vec_3f_t sm_vec_4f_to_3f ( sm_vec_4f_t vec ) {
    sm_vec_3f_t result;
    result.x = vec.x;
    result.y = vec.y;
    result.z = vec.z;
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
