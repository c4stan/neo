#version 450

#include "xs.glsl"

layout ( binding = 0, set = xs_shader_binding_set_dispatch_m ) uniform draw_uniforms_t {
    vec3 color;
} draw_uniforms;

layout ( location = 0 ) out vec4 outColor;

void main() {
    outColor = vec4 ( draw_uniforms.color, 1.0 );
}
