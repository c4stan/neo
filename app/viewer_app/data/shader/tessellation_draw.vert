#version 450

#include "xs.glsl"
#include "common.glsl"

#include "tessellation_common.glsl"

#extension GL_EXT_scalar_block_layout : enable

//
// GPU Zen 2 - Adaptive GPU Tessellation with Compute Shaders
//

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

layout ( location = 0 ) out uvec2 out_subdivision;

// Had to re-derive this to get it to work, maybe because of CW front-face?
mat3 bit_to_xform ( uint bit ) {
    float s = float ( bit ) - 0.5;
    vec3 c1 = vec3 ( -0.5,   -s, 0 ); 
    vec3 c2 = vec3 (    s, -0.5, 0 ); 
    vec3 c3 = vec3 (  0.5,  0.5, 1 );
    return mat3 ( c1, c2, c3 );
}

mat3 key_to_xform ( uint key ) {
    mat3 xform = mat3 ( 1.0 );
    
    while ( key > 1 ) {
        xform = bit_to_xform ( key & 1 ) * xform;
        key = key >> 1;
    }

    return xform;
}

vec3 berp ( vec3 v[3], vec2 u ) {
    return v[0] + u.x * ( v[1] - v[0] ) + u.y * ( v[2] - v[0] );
}

void build_key_verts ( uint key, vec3 prim_verts[3], out vec3 out_verts[3] ) {
    mat3 xform = key_to_xform ( key );

    vec2 u1 = ( xform * vec3 ( 0, 0, 1 ) ).xy;
    vec2 u2 = ( xform * vec3 ( 0, 1, 1 ) ).xy;
    vec2 u3 = ( xform * vec3 ( 1, 0, 1 ) ).xy;

    out_verts[0] = berp ( prim_verts, u1 );
    out_verts[1] = berp ( prim_verts, u2 );
    out_verts[2] = berp ( prim_verts, u3 );
}

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

    vec3 pos = berp ( key_verts, in_pos );

    //pos.y = sin(pos.x) * sin (pos.z);
    pos.y = field_height ( pos.x, pos.z );
    gl_Position = frame_uniforms.jittered_proj_from_view * frame_uniforms.view_from_world * draw_uniforms.world_from_model * vec4 ( pos, 1 );

    out_subdivision = subdivision;
}
