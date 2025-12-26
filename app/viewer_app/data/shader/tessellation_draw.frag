#version 450

#include "xs.glsl"

#include "common.glsl"

#extension GL_EXT_debug_printf : enable

layout ( binding = 1, set = xs_shader_binding_set_dispatch_m ) uniform draw_uniforms_t {
    vec3 color;
    float roughness;
    vec3 emissive;
    float metalness;
    uint object_id;
    uint mat_id;
} draw_uniforms;

layout ( location = 0 ) out vec4 out_color;
layout ( location = 1 ) out vec4 out_nor;
layout ( location = 2 ) out vec4 out_mat;
layout ( location = 3 ) out vec3 out_rad;
layout ( location = 4 ) out uvec4 out_id;
layout ( location = 5 ) out vec2 out_vel;

void main ( void ) {
    out_color = vec4 ( 1, 0, 0, 0 );
    out_nor = vec4 ( 0, 1, 0, 1 );
    out_mat = vec4 ( 0 );
    out_rad = vec3 ( 0 );
    out_id = uvec4 ( 0 );
    out_vel = vec2 ( 0 );

    if ( !gl_FrontFacing ) {
        debugPrintfEXT ( "cull" );
    }

    vec3 normal = vec3 ( 0, 1, 0 );
    normal = normalize ( ( frame_uniforms.view_from_world * vec4 ( normal, 0 ) ).xyz );
    out_nor = vec4 ( normal * 0.5 + 0.5, 1 );
}
