#version 450

#include "xs.glsl"

#include "common.glsl"

#extension GL_EXT_debug_printf : enable

layout ( binding = 1, set = xs_shader_binding_set_dispatch_m ) uniform draw_uniforms_t {
    vec3 color;
    float roughness;
    vec3 emissive;
    float metalness;
    uint object_id;
    uint mat_id;
} draw_uniforms;

layout ( location = 0 ) in flat uvec2 in_subdivision;
layout ( location = 1 ) in vec3 in_bary;

layout ( location = 0 ) out vec4 out_color;
layout ( location = 1 ) out vec4 out_nor;
layout ( location = 2 ) out vec4 out_mat;
layout ( location = 3 ) out vec3 out_rad;
layout ( location = 4 ) out uvec4 out_id;
layout ( location = 5 ) out vec2 out_vel;

vec3 subdivision_color ( void ) {
    uint hash = hash_u32 ( in_subdivision.x ) ^ hash_u32 ( in_subdivision.y );
    return vec3 ( hash & 0xff, ( hash >> 8 ) & 0xff, ( hash >> 16 ) & 0xff ) / 255.f;
}

void main ( void ) {
    vec3 color = subdivision_color();
    //color = vec3 ( 1, 0, 0 );
    //color = in_bary;
    out_color = vec4 ( color, 0 );
    out_nor = vec4 ( 0, 1, 0, 1 );
    out_mat = vec4 ( 0 );
    out_rad = vec3 ( 0 );
    uint tri_id = in_subdivision.x; // TODO
    uint object_id = draw_uniforms.object_id;
    out_id = uvec4 ( object_id >> 8, object_id & 0xff, tri_id >> 8, tri_id & 0xff );
    out_vel = vec2 ( 0.5 );

    vec3 normal = vec3 ( 0, 1, 0 );
    normal = normalize ( ( frame_uniforms.view_from_world * vec4 ( normal, 0 ) ).xyz );
    out_nor = vec4 ( normal * 0.5 + 0.5, 1 );
}
