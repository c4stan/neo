#version 450

#include "xs.glsl"

layout ( location = 0 ) in vec4 in_color;
layout ( location = 1 ) in vec2 in_uv;

layout ( set = xs_shader_binding_set_per_draw_m, binding = 0 ) uniform texture2D tex_color;

layout ( set = xs_shader_binding_set_per_draw_m, binding = 1 ) uniform sampler tex_sampler;

layout ( location = 0 ) out vec4 out_color;

vec3 linear_to_srgb ( vec3 color ) {
    return pow ( color, vec3 ( 1.0 / 2.2 ) );
}

void main() {
    vec4 tex = texture ( sampler2D ( tex_color, tex_sampler ), in_uv ).xyzw;
    vec4 color = in_color * tex;
    color.rgb = linear_to_srgb ( color.rgb );
    out_color = color;
}
