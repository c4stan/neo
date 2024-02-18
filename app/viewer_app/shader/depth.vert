#version 450

#include "xs.glsl"

#include "common.glsl"

layout ( location = 0 ) in vec3 in_pos;

layout ( binding = 0, set = xs_resource_binding_set_per_draw_m ) uniform draw_cbuffer_t {
    mat4 world_from_model;
} draw_cbuffer;

void main() {
    vec4 pos = vec4 ( in_pos, 1.0 );
    gl_Position = view_cbuffer.proj_from_view * view_cbuffer.view_from_world * draw_cbuffer.world_from_model * pos;
}
