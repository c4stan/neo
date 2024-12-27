#version 450

#include "xs.glsl"

#include "common.glsl"

layout ( binding = 0, set = xs_shader_binding_set_per_draw_m ) uniform texture2D tex_color;

layout ( binding = 1, set = xs_shader_binding_set_per_draw_m ) uniform sampler tex_sampler;

layout ( binding = 2, set = xs_shader_binding_set_per_draw_m ) uniform draw_cbuffer_t {
    vec2 src_resolution_f32;
    vec2 dst_resolution_f32;
} draw_cbuffer;

layout ( location = 0 ) out vec4 out_color;

void main() {
    vec2 dst_to_src = draw_cbuffer.src_resolution_f32 / draw_cbuffer.dst_resolution_f32;
    vec2 src_uv = gl_FragCoord.xy * dst_to_src / draw_cbuffer.src_resolution_f32;

    vec4 color = texture ( sampler2D ( tex_color, tex_sampler ), src_uv );

    out_color = color;
}
