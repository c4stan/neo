#version 450

#include "xs.glsl"

void main ( void ) {
    vec2 vertices[3] = vec2[3] ( vec2 ( -3, -1 ), vec2 ( 0, 3 ), vec2 ( 3, -1 ) );
    gl_Position = vec4 ( vertices[gl_VertexIndex], 0, 1 );
}
