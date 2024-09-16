#version 460 core
#extension GL_NV_ray_tracing : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : enable

#include <xs.glsl>

struct instance_t {
    uint64_t idx_buffer;
    uint64_t pos_buffer;
    uint64_t nor_buffer;
};

layout ( buffer_reference, scalar ) buffer pos_buffer_t { vec3 data[]; };
layout ( buffer_reference, scalar ) buffer nor_buffer_t { vec3 data[]; };

layout ( location = 0 ) rayPayloadInNV vec4 payload;
layout ( binding = 1, set = xs_resource_binding_set_per_draw_m, rgba32f ) uniform image2D img_color;
layout ( binding = 2, set = xs_resource_binding_set_per_draw_m ) buffer instance_array_t {
    instance_t data[];
} instance_array;

void main ( void ) {

    payload = vec4 ( 0.0, 1.0, 0.0, 1.0 );
}
