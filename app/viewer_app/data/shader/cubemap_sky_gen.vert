#version 450

#include "xs.glsl"

#include "common.glsl"

layout ( binding = 0, set = xs_shader_binding_set_dispatch_m ) uniform draw_uniforms_t {
    mat4 view_from_world;
    mat4 proj_from_view;
} draw_uniforms;

layout ( location = 0 ) in vec3 in_pos;
layout ( location = 1 ) in vec3 in_nor;
layout ( location = 2 ) in vec3 in_tan;
layout ( location = 3 ) in vec3 in_bitan;
layout ( location = 4 ) in vec2 in_uv;

layout ( location = 0 ) out vec3 out_dir;

void main() {
    vec4 pos = vec4 ( in_pos, 1.0 );

    mat4 view_from_world = draw_uniforms.view_from_world;
    vec4 view_pos = view_from_world * pos;
    out_dir = in_pos.xyz;

    vec4 proj_pos = draw_uniforms.proj_from_view * view_pos;
    proj_pos.z = proj_pos.w;
    gl_Position = proj_pos;
}
