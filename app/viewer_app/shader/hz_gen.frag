#version 450

#include "xs.glsl"

#include "common.glsl"

layout ( binding = 0, set = xs_resource_binding_set_per_draw_m ) uniform texture2D tex_depth;

layout ( binding = 1, set = xs_resource_binding_set_per_draw_m ) uniform sampler sampler_point;

layout ( binding = 2, set = xs_resource_binding_set_per_draw_m ) uniform draw_cbuffer_t {
    vec2 src_resolution_f32;
    vec2 dst_resolution_f32;
} draw_cbuffer;

layout ( location = 0 ) out float out_depth;

void main() {
    vec2 dst_to_src = draw_cbuffer.src_resolution_f32 / draw_cbuffer.dst_resolution_f32;
    vec2 src_uv = gl_FragCoord.xy * dst_to_src / draw_cbuffer.src_resolution_f32;

    vec4 depths = textureGather ( sampler2D ( tex_depth, sampler_point ), src_uv );

    float min_depth = min ( min ( depths.x, depths.y ), min ( depths.z, depths.w ) );

    out_depth = min_depth;
}
