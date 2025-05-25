#version 460 core
#extension GL_NV_ray_tracing : require

#include <xs.glsl>

#include "common.glsl"

layout ( location = 0 ) rayPayloadInNV ray_payload_t ray_payload;

void main ( void ) {
    ray_payload.color = vec3 ( 0.0, 0.0, 0.0 );
    ray_payload.distance = -1;
    ray_payload.normal = vec3(0);
    ray_payload.emissive = vec3(0);
}