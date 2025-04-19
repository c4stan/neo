#version 450

#include "xs.glsl"

#include "common.glsl"

layout ( binding = 0, set = xs_shader_binding_set_dispatch_m ) uniform draw_uniforms_t {
    mat4 world_from_model;
    mat4 prev_world_from_model;
} draw_uniforms;

layout ( location = 0 ) in vec3 in_pos;
layout ( location = 1 ) in vec3 in_nor;
layout ( location = 2 ) in vec3 in_tan;
layout ( location = 3 ) in vec3 in_bitan;
layout ( location = 4 ) in vec2 in_uv;

layout ( location = 0 ) out vec3 out_pos;
layout ( location = 1 ) out vec3 out_nor;
layout ( location = 2 ) out vec3 out_t;
layout ( location = 3 ) out vec3 out_b;
layout ( location = 4 ) out vec3 out_n;
layout ( location = 5 ) out vec2 out_uv;
layout ( location = 6 ) out vec4 out_curr_clip_pos;
layout ( location = 7 ) out vec4 out_prev_clip_pos;

void main() {
    vec4 pos = vec4 ( in_pos, 1.0 );

    gl_Position = frame_uniforms.jittered_proj_from_view * frame_uniforms.view_from_world * draw_uniforms.world_from_model * pos;

    out_pos = ( frame_uniforms.view_from_world * draw_uniforms.world_from_model * pos ).xyz;
    out_nor = normalize ( mat3 ( frame_uniforms.view_from_world * draw_uniforms.world_from_model ) * in_nor );
    out_t = normalize ( ( draw_uniforms.world_from_model * vec4 ( in_tan, 0 ) ).xyz );
    out_b = normalize ( ( draw_uniforms.world_from_model * vec4 ( in_bitan, 0 ) ).xyz );
    out_n = normalize ( ( draw_uniforms.world_from_model * vec4 ( in_nor, 0 ) ).xyz );
    out_uv = in_uv;
    out_curr_clip_pos = ( frame_uniforms.proj_from_view * frame_uniforms.view_from_world * draw_uniforms.world_from_model * pos ).xyzw;
    out_prev_clip_pos = ( frame_uniforms.prev_proj_from_view * frame_uniforms.prev_view_from_world * draw_uniforms.prev_world_from_model * pos ).xyzw;
}
