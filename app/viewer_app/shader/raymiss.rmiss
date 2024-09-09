#version 460 core
#extension GL_NV_ray_tracing : require

#include <xs.glsl>

layout ( location = 0 ) rayPayloadInNV vec4 payload;

void main ( void ) {
    payload = vec4 ( 1.0, 0.0, 0.0, 1.0 );
}