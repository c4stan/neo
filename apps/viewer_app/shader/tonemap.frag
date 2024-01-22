#version 450

#include "xs.glsl"

#include "common.glsl"

layout ( set = xs_resource_binding_set_per_draw_m, binding = 0 ) uniform texture2D tex_color;

layout ( set = xs_resource_binding_set_per_draw_m, binding = 1 ) uniform sampler sampler_point;

layout ( location = 0 ) out vec4 out_color;

void main ( void ) {
    // screen uv
    vec2 screen_uv = vec2 ( gl_FragCoord.xy / frame_cbuffer.resolution_f32 );

    // sample
    vec3 color = texture ( sampler2D ( tex_color, sampler_point ), screen_uv ).xyz;

    // tonemap
    float luma = rgb_to_luma ( color );
    //color = 1.0.xxx - exp ( -color );
    color = color / ( 1 + luma );

    out_color = vec4 ( linear_to_srgb ( color ), 1 );
}
