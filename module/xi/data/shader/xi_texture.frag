#version 450

#include "xs.glsl"

layout ( location = 0 ) in vec4 in_color;
layout ( location = 1 ) in vec2 in_uv;

layout ( set = xs_shader_binding_set_dispatch_m, binding = 0 ) uniform texture2D tex_color;

layout ( set = xs_shader_binding_set_dispatch_m, binding = 1 ) uniform sampler tex_sampler;

layout ( set = xs_shader_binding_set_dispatch_m, binding = 2 ) uniform draw_uniforms_t {
    ivec4 remap;
    vec4 fallback;
} draw_uniforms;

layout ( location = 0 ) out vec4 out_color;

vec3 linear_to_srgb ( vec3 color ) {
    return pow ( color, vec3 ( 1.0 / 2.2 ) );
}

void main() {
    vec4 tex = texture ( sampler2D ( tex_color, tex_sampler ), in_uv ).xyzw;
    
    vec4 color;
    ivec4 remap = draw_uniforms.remap;
    vec4 fallback = draw_uniforms.fallback;
    color[0] = remap[0] < 4 ? tex[remap[0]] : fallback[0];
    color[1] = remap[1] < 4 ? tex[remap[1]] : fallback[1];
    color[2] = remap[2] < 4 ? tex[remap[2]] : fallback[2];
    color[3] = remap[3] < 4 ? tex[remap[3]] : fallback[3];

    color *= in_color;

    // TODO do this in a final full screen pass, otherwise alpha blended UI is broken
    color.rgb = linear_to_srgb ( color.rgb );
    out_color = color;
}
