#version 460 core
#extension GL_NV_ray_tracing : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : enable

#include <xs.glsl>

hitAttributeNV vec2 bary_uv;

struct instance_t {
    uint64_t idx_buffer;
    uint64_t pos_buffer;
    uint64_t nor_buffer;
    float albedo[3];
};

layout ( buffer_reference, scalar ) buffer float3_buffer_t { float[3] data[]; };
layout ( buffer_reference, scalar ) buffer uint3_buffer_t { uint[3] data[]; };

layout ( location = 0 ) rayPayloadInNV vec4 payload;
layout ( location = 1 ) rayPayloadNV bool is_shadowed;

layout ( binding = 0, set = xs_resource_binding_set_per_draw_m ) uniform accelerationStructureNV scene;

struct light_t {
    vec3 pos;
    float emissive;
    vec3 color;
    uint _pad0;
    mat4 proj_from_view;
    mat4 view_from_world;
};

#define MAX_LIGHT_COUNT 32

layout ( binding = 3, set = xs_resource_binding_set_per_draw_m ) uniform draw_cbuffer_t {
    uint light_count;
    uint _pad0;
    uint _pad1;
    uint _pad2;
    light_t lights[MAX_LIGHT_COUNT];
} draw_cbuffer;

layout ( binding = 2, set = xs_resource_binding_set_per_draw_m, scalar ) buffer instance_array_t {
    instance_t data[];
} instance_array;

layout ( binding = 1, set = xs_resource_binding_set_per_draw_m, rgba32f ) uniform image2D img_color;

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
    vec3 world_normal = vec3 ( model_normal * gl_WorldToObjectNV ); // ??
    world_normal = normalize ( world_normal );

    vec3 base_color = load_vec3 ( instance.albedo );

    vec3 irradiance = vec3 ( 0, 0, 0 );

    uint i = 0;
    for ( uint i = 0; i < draw_cbuffer.light_count; ++i ) {
        vec3 world_light_pos = draw_cbuffer.lights[i].pos;
        float light_emissive = draw_cbuffer.lights[i].emissive;
        vec3 light_color = draw_cbuffer.lights[i].color;

        vec3 world_light_dir = normalize ( world_light_pos - world_pos );
        float nl = clamp ( dot ( world_light_dir, world_normal ), 0, 1 );
        float d = distance ( world_pos, world_light_pos );

        if ( dot ( world_light_dir, world_normal ) > 0 )
        {
            float tMin   = 0.001;
            float tMax   = d;
            vec3  origin = gl_WorldRayOriginNV + gl_WorldRayDirectionNV * gl_HitTNV;
            vec3  rayDir = world_light_dir;
            uint  flags  = gl_RayFlagsTerminateOnFirstHitNV | gl_RayFlagsOpaqueNV | gl_RayFlagsSkipClosestHitShaderNV;
            is_shadowed  = true;
            traceNV ( scene,        // acceleration structure
                        flags,      // rayFlags
                        0xFF,       // cullMask
                        0,          // sbtRecordOffset
                        0,          // sbtRecordStride
                        1,          // missIndex
                        origin,     // ray origin
                        tMin,       // ray min range
                        rayDir,     // ray direction
                        tMax,       // ray max range
                        1           // payload (location = 1)
            );
        }

        float light_visibility = 1;

        if ( is_shadowed ) {
            light_visibility = 0.3;
        }

        irradiance += light_visibility * light_emissive * light_color * base_color * nl / ( d * d );
    }

    vec3 direct = irradiance;

    payload = vec4 ( direct, 1.0 );
}
