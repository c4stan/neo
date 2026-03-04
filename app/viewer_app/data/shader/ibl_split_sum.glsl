vec3 ggx_sample_h ( vec2 e, vec3 wo, vec3 normal, float roughness ) {
    float theta = atan ( roughness * sqrt ( e.x / ( 1.f - e.x ) ) );
    float phi = PI * 2.f * e.y;
    vec3 h = vec3_from_spherical ( theta, phi );

    mat3 tnb = tnb_from_normal ( normal );
    h = normalize ( tnb * h );
    return h;
}

vec3 ggx_sample_wi ( vec2 e, vec3 wo, vec3 normal, float roughness ) {
    vec3 h = ggx_sample_h ( e, wo, normal, roughness );
    vec3 wi = ( h * dot ( wo, h ) * 2 ) - wo;
    return wi;
}
