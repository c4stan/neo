#version 450

#include "xs.glsl"

#include "common.glsl"

layout ( binding = 0, set = xs_resource_binding_set_per_draw_m ) uniform texture2D tex_forward;
layout ( binding = 1, set = xs_resource_binding_set_per_draw_m ) uniform texture2D tex_ssr;
layout ( binding = 2, set = xs_resource_binding_set_per_draw_m ) uniform texture2D tex_ssgi;
layout ( binding = 3, set = xs_resource_binding_set_per_draw_m ) uniform texture2D tex_ssgi_2;

layout ( binding = 4, set = xs_resource_binding_set_per_draw_m ) uniform sampler sampler_point;

layout ( location = 0 ) out vec4 out_color;

void main ( void ) {
    vec2 screen_uv = vec2 ( gl_FragCoord.xy / frame_cbuffer.resolution_f32 );

    vec3 forward = texture ( sampler2D ( tex_forward, sampler_point ), screen_uv ).xyz;
    vec3 ssr = texture ( sampler2D ( tex_ssr, sampler_point ), screen_uv ).xyz;
    vec3 ssgi = texture ( sampler2D ( tex_ssgi, sampler_point ), screen_uv ).xyz;
    vec3 ssgi_2 = texture ( sampler2D ( tex_ssgi_2, sampler_point ), screen_uv ).xyz;

    vec3 combine = forward + ssr + ssgi + ssgi_2;

    out_color = vec4 ( combine, 1 );
}
