#version 450

#include "xs.glsl"

layout ( binding = 0, set = xs_shader_binding_set_per_draw_m ) uniform draw_cbuffer_t {
    vec3 color;
} draw_cbuffer;

layout ( location = 0 ) out vec4 outColor;

void main() {
    outColor = vec4 ( draw_cbuffer.color, 1.0 );
}
