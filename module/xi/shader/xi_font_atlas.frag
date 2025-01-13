#version 450

#include "xs.glsl"

layout ( binding = 0, set = xs_shader_binding_set_dispatch_m ) uniform draw_cbuffer_t {
    vec3 color;
    vec3 outline_color;
    bool outline; // TODO permutation
    vec2 resolution_f32;
} draw_cbuffer;

layout ( binding = 1, set = xs_shader_binding_set_dispatch_m ) uniform utexture2D tex_atlas;

layout ( binding = 2, set = xs_shader_binding_set_dispatch_m ) uniform sampler sampler_point;

layout ( location = 0 ) out vec4 out_color;

void main ( void ) {
    vec2 screen_uv = vec2 ( gl_FragCoord.xy / draw_cbuffer.resolution_f32 );

    uint source = texture ( usampler2D ( tex_atlas, sampler_point ), screen_uv ).x;

    vec4 color = vec4 ( 0, 0, 0, 0 );

    if ( source > 0 ) {
        color.xyz = draw_cbuffer.color;
        color.w = source / 255.0;

    } else {
        if ( draw_cbuffer.outline ) {
            uint n00 = texture ( usampler2D ( tex_atlas, sampler_point ), ( gl_FragCoord.xy + vec2 ( -1, -1 ) ) / draw_cbuffer.resolution_f32 ).x;
            uint n10 = texture ( usampler2D ( tex_atlas, sampler_point ), ( gl_FragCoord.xy + vec2 (  0, -1 ) ) / draw_cbuffer.resolution_f32 ).x;
            uint n20 = texture ( usampler2D ( tex_atlas, sampler_point ), ( gl_FragCoord.xy + vec2 ( +1, -1 ) ) / draw_cbuffer.resolution_f32 ).x;
            uint n01 = texture ( usampler2D ( tex_atlas, sampler_point ), ( gl_FragCoord.xy + vec2 ( -1,  0 ) ) / draw_cbuffer.resolution_f32 ).x;
            uint n21 = texture ( usampler2D ( tex_atlas, sampler_point ), ( gl_FragCoord.xy + vec2 ( +1,  0 ) ) / draw_cbuffer.resolution_f32 ).x;
            uint n02 = texture ( usampler2D ( tex_atlas, sampler_point ), ( gl_FragCoord.xy + vec2 ( -1, +1 ) ) / draw_cbuffer.resolution_f32 ).x;
            uint n12 = texture ( usampler2D ( tex_atlas, sampler_point ), ( gl_FragCoord.xy + vec2 (  0, +1 ) ) / draw_cbuffer.resolution_f32 ).x;
            uint n22 = texture ( usampler2D ( tex_atlas, sampler_point ), ( gl_FragCoord.xy + vec2 ( +1, +1 ) ) / draw_cbuffer.resolution_f32 ).x;

            uint s = n00 + n10 + n20 + n01 + n21 + n02 + n12 + n22;

            if ( s > 0 ) {
                color.xyz = draw_cbuffer.outline_color;
                color.w = 1;
            }
        }
    }

    out_color = color;
}
