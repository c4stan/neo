#version 450

#include "xs.glsl"
#include "common.glsl"

#include "tessellation_common.glsl"

#extension GL_EXT_scalar_block_layout : enable

layout ( binding = 0, set = xs_shader_binding_set_dispatch_m ) uniform draw_uniforms_t {
    mat4 world_from_model;
    mat4 prev_world_from_model;
} draw_uniforms;

layout ( binding = 2, set = xs_shader_binding_set_dispatch_m, scalar ) buffer readonly vertex_buffer_t {
    float data[];
} vertex_buffer;

layout ( binding = 3, set = xs_shader_binding_set_dispatch_m, scalar ) buffer readonly index_buffer_t {
    uint data[];
} index_buffer;

layout ( binding = 4, set = xs_shader_binding_set_dispatch_m, scalar ) buffer readonly subdivision_buffer_t {
    uint _pad0[4];
    uvec2 data[];
} subdivision_buffer;

layout ( location = 0 ) in vec2 in_pos;

layout ( location = 0 ) out vec3 out_nor;
layout ( location = 1 ) out vec4 out_curr_clip_pos;
layout ( location = 2 ) out vec4 out_prev_clip_pos;
layout ( location = 3 ) out uvec2 out_subdivision;
layout ( location = 4 ) out vec3 out_bary;

vec3 load_vert ( uint vert_id ) {
    uint idx = index_buffer.data[vert_id];
    vec3 pos = vec3 (
        vertex_buffer.data[idx * 3 + 0],
        vertex_buffer.data[idx * 3 + 1],
        vertex_buffer.data[idx * 3 + 2]
    );
    return pos;
}

void build_prim_verts ( uint prim_id, out vec3 out_verts[3] ) {
    out_verts[0] = load_vert ( prim_id * 3 + 0 );
    out_verts[1] = load_vert ( prim_id * 3 + 1 );
    out_verts[2] = load_vert ( prim_id * 3 + 2 );
}

void main ( void ) {
    uint instance_id = gl_InstanceIndex;
    uvec2 subdivision = subdivision_buffer.data[instance_id];

    uint prim_id = subdivision.y;
    vec3 prim_verts[3];
    build_prim_verts ( prim_id, prim_verts );

    uint key = subdivision.x;
    vec3 key_verts[3];
    build_key_verts ( key, prim_verts, key_verts );

    vec4 pos = vec4 ( berp ( key_verts, in_pos ), 1 );

    //pos.y = sin(pos.x) * sin (pos.z);
    pos.y += field_height ( pos.x, pos.z );
    float normal_step = 0.05f;
    float hx = field_height ( pos.x - normal_step, pos.z ) - field_height ( pos.x + normal_step, pos.z );
    float hz = field_height ( pos.x, pos.z - normal_step ) - field_height ( pos.x, pos.z + normal_step );
    vec3 normal = normalize ( vec3 ( hx, 2.f * normal_step, hz ) );

    gl_Position = frame_uniforms.jittered_proj_from_view * frame_uniforms.view_from_world * draw_uniforms.world_from_model * pos;
    out_nor = normalize ( mat3 ( frame_uniforms.view_from_world * draw_uniforms.world_from_model ) * normal );
    out_curr_clip_pos = ( frame_uniforms.proj_from_view * frame_uniforms.view_from_world * draw_uniforms.world_from_model * pos ).xyzw;
    out_prev_clip_pos = ( frame_uniforms.prev_proj_from_view * frame_uniforms.prev_view_from_world * draw_uniforms.prev_world_from_model * pos ).xyzw;

    out_subdivision = subdivision;
    vec3 bary[3];
    bary[0] = vec3 ( 1, 0, 0 );
    bary[1] = vec3 ( 0, 1, 0 );
    bary[2] = vec3 ( 0, 0, 1 );
    out_bary = berp ( bary, in_pos );
}
