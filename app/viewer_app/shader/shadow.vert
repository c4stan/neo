#version 450

#include "xs.glsl"

#include "common.glsl"

layout ( location = 0 ) in vec3 in_pos;

layout ( binding = 0, set = xs_shader_binding_set_pass_m ) uniform pass_uniforms_t {
    mat4 view_from_world;
    mat4 proj_from_view;
} pass_uniforms;

layout ( binding = 0, set = xs_shader_binding_set_dispatch_m ) uniform draw_uniforms_t {
    mat4 world_from_model;
} draw_uniforms;

void main() {
    vec4 pos = vec4 ( in_pos, 1.0 );
    gl_Position = pass_uniforms.proj_from_view * pass_uniforms.view_from_world * draw_uniforms.world_from_model * pos;
    //gl_Position = frame_cbuffer.jittered_proj_from_view * frame_cbuffer.view_from_world * draw_cbuffer.world_from_model * pos;
}
