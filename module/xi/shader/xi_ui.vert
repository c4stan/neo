#version 450

#include "xs.glsl"

layout ( location = 0 ) in vec2 in_pos;
layout ( location = 1 ) in vec2 in_uv;
layout ( location = 2 ) in vec4 in_color;

layout ( location = 0 ) out vec4 out_color;
layout ( location = 1 ) out vec2 out_uv;

void main() {
    out_color = in_color;
    out_uv = in_uv;
    gl_Position = vec4 ( in_pos, 0, 1 );
}
