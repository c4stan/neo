#version 450

#include "xs.glsl"

#include "common.glsl"

layout ( binding = 1, set = xs_shader_binding_set_dispatch_m ) uniform draw_cbuffer_t {
    vec3 color;
    uint object_id;
    float roughness;
    float metalness;
} draw_cbuffer;

layout ( binding = 2, set = xs_shader_binding_set_dispatch_m ) uniform texture2D color_texture;

layout ( binding = 3, set = xs_shader_binding_set_dispatch_m ) uniform sampler sampler_linear;

layout ( location = 0 ) in vec3 in_pos;
layout ( location = 1 ) in vec3 in_nor;
layout ( location = 2 ) in vec2 in_uv;
layout ( location = 3 ) in vec4 in_curr_clip_pos;
layout ( location = 4 ) in vec4 in_prev_clip_pos;

layout ( location = 0 ) out vec4 out_color;
layout ( location = 1 ) out vec4 out_nor;
layout ( location = 2 ) out uvec4 out_id;
layout ( location = 3 ) out vec2 out_vel;

void main() {
    // color
    vec4 texture_color = texture ( sampler2D ( color_texture, sampler_linear ), in_uv );
    out_color = vec4 ( draw_cbuffer.color * texture_color.xyz, draw_cbuffer.metalness );
    
    // normals
    float backface_flip = gl_FrontFacing ? 1.f : -1.f;
    out_nor = vec4 ( vec3 ( in_nor * 0.5 * backface_flip + 0.5 ), draw_cbuffer.roughness );
    
    // obj id
    uint tri_id = gl_PrimitiveID;
    out_id = uvec4 ( draw_cbuffer.object_id, tri_id >> 8, tri_id & 0xff, 0 );
    
    // velocity
    vec2 curr_vel_pos = in_curr_clip_pos.xy / in_curr_clip_pos.w;
    vec2 prev_vel_pos = in_prev_clip_pos.xy / in_prev_clip_pos.w;
    vec2 velocity = curr_vel_pos.xy - prev_vel_pos.xy;
    velocity = velocity * vec2 ( 0.5, -0.5 );
    out_vel = velocity;
}
