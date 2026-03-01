#version 450

#include "xs.glsl"

#include "common.glsl"

layout ( binding = 1, set = xs_shader_binding_set_dispatch_m ) uniform texture2D color_texture;
layout ( binding = 2, set = xs_shader_binding_set_dispatch_m ) uniform sampler sampler_linear;

layout ( location = 0 ) in vec3 in_dir;

layout ( location = 0 ) out vec4 out_color;

void main() {
    vec3 dir = normalize ( in_dir );
    vec2 sample_uv = equirectangular_uv_from_dir ( dir );
    out_color = texture ( sampler2D ( color_texture, sampler_linear ), sample_uv );
}
