#version 450

#include "xs.glsl"

#include "common.glsl"

layout ( binding = 0, set = xs_shader_binding_set_dispatch_m ) uniform draw_cbuffer_t {
    mat4 world_from_model;
    mat4 prev_world_from_model;
} draw_cbuffer;

layout ( location = 0 ) in vec3 in_pos;
layout ( location = 1 ) in vec3 in_nor;
layout ( location = 2 ) in vec2 in_uv;

layout ( location = 0 ) out vec3 out_pos;
layout ( location = 1 ) out vec3 out_nor;
layout ( location = 2 ) out vec2 out_uv;
layout ( location = 3 ) out vec4 out_curr_clip_pos;
layout ( location = 4 ) out vec4 out_prev_clip_pos;

void main() {
    vec4 pos = vec4 ( in_pos, 1.0 );

    gl_Position = frame_cbuffer.jittered_proj_from_view * frame_cbuffer.view_from_world * draw_cbuffer.world_from_model * pos;

    out_pos = ( frame_cbuffer.view_from_world * draw_cbuffer.world_from_model * pos ).xyz;
    out_nor = normalize ( mat3 ( frame_cbuffer.view_from_world * draw_cbuffer.world_from_model ) * in_nor );
    out_uv = in_uv;
    out_curr_clip_pos = ( frame_cbuffer.proj_from_view * frame_cbuffer.view_from_world * draw_cbuffer.world_from_model * pos ).xyzw;
    out_prev_clip_pos = ( frame_cbuffer.prev_proj_from_view * frame_cbuffer.prev_view_from_world * draw_cbuffer.prev_world_from_model * pos ).xyzw;
}
