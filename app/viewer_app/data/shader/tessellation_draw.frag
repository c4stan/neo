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

layout ( location = 0 ) in vec3 in_nor;
layout ( location = 1 ) in vec4 in_curr_clip_pos;
layout ( location = 2 ) in vec4 in_prev_clip_pos;
layout ( location = 3 ) in flat uvec2 in_subdivision;
layout ( location = 4 ) in vec3 in_bary;

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
    // color
    vec3 color = subdivision_color();
    color = vec3 ( 1, 1, 1 );
    //color = in_bary;
    out_color = vec4 ( color, 1 );
    
    // normal
    float backface_flip = gl_FrontFacing ? 1.f : -1.f;
    out_nor = vec4 ( vec3 ( in_nor * 0.5 * backface_flip + 0.5 ), draw_uniforms.roughness );
    
    // material
    float mat_id = draw_uniforms.mat_id;
    vec3 mat_data = vec3 ( 0, 0, 0 );
    out_mat = vec4 ( mat_id, mat_data );

    // radiosity
    out_rad = draw_uniforms.emissive;

    // obj id
    uint tri_id = in_subdivision.x; // TODO
    uint object_id = draw_uniforms.object_id;
    out_id = uvec4 ( object_id >> 8, object_id & 0xff, tri_id >> 8, tri_id & 0xff );
    out_vel = vec2 ( 0.5 );

    // velocity
    vec2 curr_vel_pos = in_curr_clip_pos.xy / in_curr_clip_pos.w;
    vec2 prev_vel_pos = in_prev_clip_pos.xy / in_prev_clip_pos.w;
    vec2 velocity = curr_vel_pos.xy - prev_vel_pos.xy;
    velocity = velocity * vec2 ( 0.5, -0.5 ) + 0.5;
    out_vel = velocity;
}
