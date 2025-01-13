#version 450

#include "xs.glsl"

layout ( binding = 0, set = xs_shader_binding_set_dispatch_m ) uniform draw_uniforms_t {
    mat4 world_from_model;
    mat4 proj_from_world;
    vec4 color;
} draw_uniforms;

layout ( location = 0 ) in vec3 in_pos;
layout ( location = 1 ) in vec3 in_nor;
layout ( location = 2 ) in vec2 in_uv;

layout ( location = 0 ) out vec3 out_nor;
layout ( location = 1 ) out vec2 out_uv;

void main() {
    vec4 pos = vec4 ( in_pos, 1.0 );
    gl_Position = draw_uniforms.proj_from_world * draw_uniforms.world_from_model * pos;

    out_nor = normalize ( mat3 ( draw_uniforms.world_from_model ) * in_nor );
    out_uv = in_uv;
}
