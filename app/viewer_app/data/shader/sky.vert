#version 450

#include "xs.glsl"

#include "common.glsl"

layout ( location = 0 ) in vec3 in_pos;
layout ( location = 1 ) in vec3 in_nor;
layout ( location = 2 ) in vec3 in_tan;
layout ( location = 3 ) in vec3 in_bitan;
layout ( location = 4 ) in vec2 in_uv;

layout ( location = 0 ) out vec3 out_dir;

void main() {
    vec4 pos = vec4 ( in_pos, 1.0 );

    mat4 view_from_world = frame_uniforms.view_from_world;
    view_from_world[3].xyz = vec3 ( 0 );

    vec4 view_pos = view_from_world * pos;
    out_dir = in_pos.xyz;

    vec4 proj_pos = frame_uniforms.jittered_proj_from_view * view_pos;
#if reverse_depth_m
    proj_pos.z = 0;
#else
    proj_pos.z = proj_pos.w;
#endif
    gl_Position = proj_pos;
}
