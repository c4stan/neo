#version 450

#include "xs.glsl"

#include "common.glsl"

layout ( binding = 1, set = xs_shader_binding_set_dispatch_m ) uniform draw_cbuffer_t {
    vec3 color;
    uint object_id;
    float roughness;
    float metalness;
} draw_cbuffer;

layout ( location = 0 ) in vec3 in_pos;
layout ( location = 1 ) in vec3 in_nor;

layout ( location = 0 ) out vec4 out_color;
layout ( location = 1 ) out vec4 out_nor;
layout ( location = 2 ) out uvec4 out_id;

void main() {
    out_color = vec4 ( draw_cbuffer.color, draw_cbuffer.metalness );
    float backface_flip = gl_FrontFacing ? 1.f : -1.f;
    out_nor = vec4 ( vec3 ( in_nor * 0.5 * backface_flip + 0.5 ), draw_cbuffer.roughness );
    uint tri_id = gl_PrimitiveID;
    out_id = uvec4 ( draw_cbuffer.object_id, tri_id >> 8, tri_id & 0xff, 0 );
}
