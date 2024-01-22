#version 450

#include "xs.glsl"

#include "common.glsl"

layout ( set = xs_resource_binding_set_per_draw_m, binding = 0 ) uniform texture2D tex_input;
layout ( set = xs_resource_binding_set_per_draw_m, binding = 1 ) uniform sampler sampler_input;

layout ( location = 0 ) out vec4 out_color;

void main ( void ) {
    vec2 screen_uv = vec2 ( gl_FragCoord.xy / frame_cbuffer.resolution_f32 );

    vec4 sample_value = texture ( sampler2D ( tex_input, sampler_input ), screen_uv );

    out_color = sample_value;
}
