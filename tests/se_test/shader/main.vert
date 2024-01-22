#version 450

#include "xs.glsl"

layout ( binding = 0, set = xs_resource_binding_set_per_draw_m )
uniform object_cbuffer_t {
    vec2 pos;
    float scale;
} object_cbuffer;

vec2 positions[3] =
    vec2[] (
        vec2 ( -0.5, -0.5 ),
        vec2 ( 0.0, 0.5 ),
        vec2 ( 0.5, -0.5 )
    );

void main() {
    vec2 pos = object_cbuffer.pos;
    pos += positions[gl_VertexIndex] * object_cbuffer.scale;
    gl_Position = vec4 ( pos, 0.0, 1.0 );
}
