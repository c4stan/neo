#version 450

#include "xs.glsl"

layout ( binding = 0, set = xs_shader_binding_set_dispatch_m )
uniform object_uniforms_t {
    vec2 pos;
    float scale;
} object_uniforms;

vec2 positions[3] =
    vec2[] (
        vec2 ( -0.5, -0.5 ),
        vec2 ( 0.0, 0.5 ),
        vec2 ( 0.5, -0.5 )
    );

void main() {
    vec2 pos = object_uniforms.pos;
    pos += positions[gl_VertexIndex] * object_uniforms.scale;
    gl_Position = vec4 ( pos, 0.0, 1.0 );
}
