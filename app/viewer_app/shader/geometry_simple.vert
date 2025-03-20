#version 450

#include "xs.glsl"

#include "common.glsl"

layout ( binding = 0, set = xs_shader_binding_set_dispatch_m ) uniform draw_cbuffer_t {
    mat4 world_from_model;
} draw_cbuffer;

layout ( location = 0 ) in vec3 in_pos;

layout ( location = 0 ) out vec3 out_pos;

void main() {
    vec4 pos = vec4 ( in_pos, 1.0 );
    gl_Position = frame_cbuffer.jittered_proj_from_view * frame_cbuffer.view_from_world * draw_cbuffer.world_from_model * pos;
}
