#version 450

#include "xs.glsl"

layout ( location = 0 ) in vec3 in_nor;
layout ( location = 1 ) in vec2 in_uv;

layout ( binding = 0, set = xs_shader_binding_set_per_draw_m ) uniform draw_uniforms_t {
    mat4 world_from_model;
    mat4 proj_from_world;
    vec4 color;
} draw_uniforms;

layout ( set = xs_shader_binding_set_per_draw_m, binding = 1 ) uniform texture2D tex_color;
layout ( set = xs_shader_binding_set_per_draw_m, binding = 2 ) uniform sampler tex_sampler;

layout ( location = 0 ) out vec4 out_color;

void main() {
    vec4 tex = texture ( sampler2D ( tex_color, tex_sampler ), in_uv ).xyzw;
    vec4 color = draw_uniforms.color * tex;
    out_color = color;
}
