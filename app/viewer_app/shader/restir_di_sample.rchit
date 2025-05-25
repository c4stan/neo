#version 460 core
#extension GL_NV_ray_tracing : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : enable

#include <xs.glsl>

#include "common.glsl"

hitAttributeNV vec2 bary_uv;

struct instance_t {
    uint64_t idx_buffer;
    uint64_t pos_buffer;
    uint64_t nor_buffer;
    float albedo[3];
    float emissive[3];
    uint id;
};

layout ( buffer_reference, scalar ) buffer float3_buffer_t { float[3] data[]; };
layout ( buffer_reference, scalar ) buffer uint3_buffer_t { uint[3] data[]; };

layout ( location = 0 ) rayPayloadInNV ray_payload_t ray_payload;

layout ( binding = 1, set = xs_shader_binding_set_dispatch_m, scalar ) buffer instance_array_t {
    instance_t data[];
} instance_array;

vec3 load_vec3 ( float[3] f32 ) {
    return vec3 ( f32[0], f32[1], f32[2] );
}

uvec3 load_uvec3 ( uint[3] u32 ) {
    return uvec3 ( u32[0], u32[1], u32[2] );
}

void main ( void ) {
    instance_t instance = instance_array.data[gl_InstanceCustomIndexNV];

    uint3_buffer_t idx_buffer = uint3_buffer_t ( instance.idx_buffer );
    float3_buffer_t pos_buffer = float3_buffer_t ( instance.pos_buffer );
    float3_buffer_t nor_buffer = float3_buffer_t ( instance.nor_buffer );

    uvec3 idx = load_uvec3 ( idx_buffer.data[gl_PrimitiveID] );
    vec3 v0 = load_vec3 ( pos_buffer.data[idx.x] );
    vec3 v1 = load_vec3 ( pos_buffer.data[idx.y] );
    vec3 v2 = load_vec3 ( pos_buffer.data[idx.z] );
    vec3 n0 = load_vec3 ( nor_buffer.data[idx.x] );
    vec3 n1 = load_vec3 ( nor_buffer.data[idx.y] );
    vec3 n2 = load_vec3 ( nor_buffer.data[idx.z] );

    vec3 bary = vec3 ( 1.0 - bary_uv.x - bary_uv.y, bary_uv.x, bary_uv.y );

    vec3 model_pos = v0 * bary.x + v1 * bary.y + v2 * bary.z;
    vec3 world_pos = vec3 ( gl_ObjectToWorldNV * vec4 ( model_pos, 1.0 ) );

    vec3 model_normal = n0 * bary.x + n1 * bary.y + n2 * bary.z;
    //vec3 world_normal = vec3 ( model_normal * gl_WorldToObjectNV );
    vec3 world_normal = vec3 ( mat3 ( gl_ObjectToWorldNV ) * model_normal );
    world_normal = normalize ( world_normal );

    vec3 base_color = load_vec3 ( instance.albedo );
    vec3 emissive = load_vec3 ( instance.emissive );
    uint id = instance.id;

    ray_payload.color = base_color;
    ray_payload.distance = gl_HitTNV;
    ray_payload.normal = world_normal;
    ray_payload.emissive = emissive;
    ray_payload.id = id;
}
