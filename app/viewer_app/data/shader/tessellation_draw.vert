#version 450

#include "xs.glsl"
#include "common.glsl"

#extension GL_EXT_scalar_block_layout : enable

//
// GPU Zen 2 - Adaptive GPU Tessellation with Compute Shaders
//

layout ( binding = 0, set = xs_shader_binding_set_dispatch_m ) uniform draw_uniforms_t {
    mat4 world_from_model;
    mat4 prev_world_from_model;
} draw_uniforms;

struct vertex_t {
    float pos[3];
};

layout ( binding = 2, set = xs_shader_binding_set_dispatch_m, scalar ) buffer readonly vertex_buffer_t {
    vertex_t data[];
} vertex_buffer;

layout ( binding = 3, set = xs_shader_binding_set_dispatch_m, scalar ) buffer readonly index_buffer_t {
    uint data[];
} index_buffer;

layout ( binding = 4, set = xs_shader_binding_set_dispatch_m, scalar ) buffer readonly subdivision_buffer_t {
    uint _pad0[4];
    uvec2 data[];
} subdivision_buffer;

layout ( location = 0 ) in vec2 in_pos;

////
//
// https://mrl.cs.nyu.edu/~perlin/noise/
float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
float lerp(float t, float a, float b) { return a + t * (b - a); }
float grad(int hash, float x, float y, float z) {
  int h = hash & 15;                      // CONVERT LO 4 BITS OF HASH CODE
  float u = h<8 ? x : y,                  // INTO 12 GRADIENT DIRECTIONS.
        v = h<4 ? y : h==12||h==14 ? x : z;
  return ((h&1) == 0 ? u : -u) + ((h&2) == 0 ? v : -v);
}
float noise(float x, float y, float z) {
    int p[] = { 151,160,137,91,90,15,
        131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
        190, 6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,
        88,237,149,56,87,174,20,125,136,171,168, 68,175,74,165,71,134,139,48,27,166,
        77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,55,46,245,40,244,
        102,143,54, 65,25,63,161, 1,216,80,73,209,76,132,187,208, 89,18,169,200,196,
        135,130,116,188,159,86,164,100,109,198,173,186, 3,64,52,217,226,250,124,123,
        5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,
        223,183,170,213,119,248,152, 2,44,154,163, 70,221,153,101,155,167, 43,172,9,
        129,22,39,253, 19,98,108,110,79,113,224,232,178,185, 112,104,218,246,97,228,
        251,34,242,193,238,210,144,12,191,179,162,241, 81,51,145,235,249,14,239,107,
        49,192,214, 31,181,199,106,157,184, 84,204,176,115,121,50,45,127, 4,150,254,
        138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
        151,160,137,91,90,15,
        131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
        190, 6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,
        88,237,149,56,87,174,20,125,136,171,168, 68,175,74,165,71,134,139,48,27,166,
        77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,55,46,245,40,244,
        102,143,54, 65,25,63,161, 1,216,80,73,209,76,132,187,208, 89,18,169,200,196,
        135,130,116,188,159,86,164,100,109,198,173,186, 3,64,52,217,226,250,124,123,
        5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,
        223,183,170,213,119,248,152, 2,44,154,163, 70,221,153,101,155,167, 43,172,9,
        129,22,39,253, 19,98,108,110,79,113,224,232,178,185, 112,104,218,246,97,228,
        251,34,242,193,238,210,144,12,191,179,162,241, 81,51,145,235,249,14,239,107,
        49,192,214, 31,181,199,106,157,184, 84,204,176,115,121,50,45,127, 4,150,254,
        138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
    };

    int X = int(floor(x)) & 255,                    // FIND UNIT CUBE THAT
        Y = int(floor(y)) & 255,                    // CONTAINS POINT.
        Z = int(floor(z)) & 255;
  x -= floor(x);                                    // FIND RELATIVE X,Y,Z
  y -= floor(y);                                    // OF POINT IN CUBE.
  z -= floor(z);
  float u = fade(x),                                // COMPUTE FADE CURVES
        v = fade(y),                                // FOR EACH OF X,Y,Z.
        w = fade(z);
  int A = p[X  ]+Y, AA = p[A]+Z, AB = p[A+1]+Z,      // HASH COORDINATES OF
      B = p[X+1]+Y, BA = p[B]+Z, BB = p[B+1]+Z;      // THE 8 CUBE CORNERS,

  return lerp(w, lerp(v, lerp(u, grad(p[AA  ], x  , y  , z   ),  // AND ADD
                                 grad(p[BA  ], x-1, y  , z   )), // BLENDED
                         lerp(u, grad(p[AB  ], x  , y-1, z   ),  // RESULTS
                                 grad(p[BB  ], x-1, y-1, z   ))),// FROM  8
                 lerp(v, lerp(u, grad(p[AA+1], x  , y  , z-1 ),  // CORNERS
                                 grad(p[BA+1], x-1, y  , z-1 )), // OF CUBE
                         lerp(u, grad(p[AB+1], x  , y-1, z-1 ),
                                 grad(p[BB+1], x-1, y-1, z-1 ))));
}
float field_height ( float x, float z ) {
    float param_scale = 0.1f;
    float height_scale = 10.f;
    return noise ( x * param_scale, 0, z * param_scale ) * height_scale;
}
//
////

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

vec3 load_vec3 ( float[3] f32 ) {
    return vec3 ( f32[0], f32[1], f32[2] );
}

vec3 load_vert ( uint vert_id ) {
    uint idx = index_buffer.data[vert_id];
    float[3] pos = vertex_buffer.data[idx].pos;
    return load_vec3 ( pos );
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
}
