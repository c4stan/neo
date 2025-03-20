#version 450

#include "xs.glsl"

#include "common.glsl"

layout ( binding = 1, set = xs_shader_binding_set_dispatch_m ) uniform draw_cbuffer_t {
    uint object_id;
} draw_cbuffer;

layout ( location = 0 ) in vec3 in_pos;

layout ( location = 0 ) out uint out_id;

void main() {
    out_id = draw_cbuffer.object_id;
}
