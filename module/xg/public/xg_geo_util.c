#include "xg_geo_util.h"

#include <std_log.h>

#include <math.h>

// http://www.songho.ca/opengl/gl_sphere.html
xg_geo_util_geometry_data_t xg_geo_util_generate_sphere ( float rad, uint32_t meridians_count, uint32_t parallels_count ) {
    float pi = 3.1415f;
    float meridian_step = 2.f * pi / meridians_count;
    float parallel_step = pi / parallels_count;

    // vbuffer
    uint64_t vertex_capacity = ( parallels_count + 1 ) * ( meridians_count + 1 );
    float* pos = std_virtual_heap_alloc_array_m ( float, vertex_capacity * 3 );
    float* nor = std_virtual_heap_alloc_array_m ( float, vertex_capacity * 3 );
    float* uv = std_virtual_heap_alloc_array_m ( float, vertex_capacity * 2 );
    uint32_t vert_count = 0;

    for ( uint32_t i = 0; i <= parallels_count; ++i ) {
        float parallel_angle = pi / 2.f - i * parallel_step; // pi/2 -> -pi/2
        float y = rad * sinf ( parallel_angle );

        for ( uint32_t j = 0; j <= meridians_count; ++j ) {
            float meridian_angle = j * meridian_step; // 0 -> 2pi
            float x = rad * cosf ( parallel_angle ) * cosf ( meridian_angle );
            float z = rad * cosf ( parallel_angle ) * sinf ( meridian_angle );

            float nx = x / rad;
            float ny = y  / rad;
            float nz = z / rad;

            float u = ( float ) j / meridians_count;
            float v = ( float ) j / parallels_count;

            pos[vert_count * 3 + 0] = x;
            pos[vert_count * 3 + 1] = y;
            pos[vert_count * 3 + 2] = z;
            nor[vert_count * 3 + 0] = nx;
            nor[vert_count * 3 + 1] = ny;
            nor[vert_count * 3 + 2] = nz;
            uv[vert_count * 2 + 0] = u;
            uv[vert_count * 2 + 1] = v;
            ++vert_count;
        }
    }

    // ibuffer
    uint32_t* idx = std_virtual_heap_alloc_array_m ( uint32_t, parallels_count * meridians_count * 6 );
    uint32_t idx_count = 0;

    for ( uint32_t i = 0; i < parallels_count; ++i ) {
        for ( uint32_t j = 0; j < meridians_count; ++j ) {
            uint32_t k1 = i * ( meridians_count + 1 ) + j;
            uint32_t k2 = ( i + 1 ) * ( meridians_count + 1 ) + j;

            if ( i != 0 ) {
                uint32_t i1 = k1;
                uint32_t i2 = k2;
                uint32_t i3 = k1 + 1;

                idx[idx_count++] = i3;
                idx[idx_count++] = i2;
                idx[idx_count++] = i1;
            }

            if ( i != parallels_count - 1 ) {
                uint32_t i1 = k1 + 1;
                uint32_t i2 = k2;
                uint32_t i3 = k2 + 1;

                idx[idx_count++] = i3;
                idx[idx_count++] = i2;
                idx[idx_count++] = i1;
            }
        }
    }

    // tangents
    float* tan = std_virtual_heap_alloc_array_m ( float, vertex_capacity * 3 );
    float* bitan = std_virtual_heap_alloc_array_m ( float, vertex_capacity * 3 );
    std_mem_zero ( tan, sizeof ( float ) * vertex_capacity * 3 );
    std_mem_zero ( bitan, sizeof ( float ) * vertex_capacity * 3 );
    for ( uint32_t i = 0; i < idx_count; i+= 3 ) {
        uint32_t i0 = idx[i + 0];
        uint32_t i1 = idx[i + 1];
        uint32_t i2 = idx[i + 2];

        float* v0 = pos + i0;
        float* v1 = pos + i1;
        float* v2 = pos + i2;

        float* uv0 = uv + i0;
        float* uv1 = uv + i1;
        float* uv2 = uv + i2;

        float e1[3] = { v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2] };
        float e2[3] = { v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2] };
        float u1[2] = { uv1[0] - uv0[0], uv1[1] - uv0[1] };
        float u2[2] = { uv2[0] - uv0[0], uv2[1] - uv0[1] };
        
        float f = 1.f / ( u1[0] * u2[1] - u2[0] * u1[1] );
        float t[3] = {
            f * ( u2[1] * e1[0] - u1[1] * e2[0] ),
            f * ( u2[1] * e1[1] - u1[1] * e2[1] ),
            f * ( u2[1] * e1[2] - u1[1] * e2[2] ),
        };
        float b[3] = {
            f * ( -u2[0] * e1[0] + u1[0] * e2[0] ),
            f * ( -u2[0] * e1[1] + u1[0] * e2[1] ),
            f * ( -u2[0] * e1[2] + u1[0] * e2[2] ),
        };

        tan[i0 * 3 + 0] += t[0];
        tan[i0 * 3 + 1] += t[1];
        tan[i0 * 3 + 2] += t[2];
        tan[i1 * 3 + 0] += t[0];
        tan[i1 * 3 + 1] += t[1];
        tan[i1 * 3 + 2] += t[2];
        tan[i2 * 3 + 0] += t[0];
        tan[i2 * 3 + 1] += t[1];
        tan[i2 * 3 + 2] += t[2];

        bitan[i0 * 3 + 0] += b[0];
        bitan[i0 * 3 + 1] += b[1];
        bitan[i0 * 3 + 2] += b[2];
        bitan[i1 * 3 + 0] += b[0];
        bitan[i1 * 3 + 1] += b[1];
        bitan[i1 * 3 + 2] += b[2];
        bitan[i2 * 3 + 0] += b[0];
        bitan[i2 * 3 + 1] += b[1];
        bitan[i2 * 3 + 2] += b[2];
    }

    for ( uint32_t i = 0; i < vertex_capacity; i += 3 ) {
        float* t = tan + i;
        float* b = bitan + i;

        float t_len = sqrtf ( t[0] * t[0] + t[1] * t[1] + t[2] * t[2] );
        t[0] *= 1.f / t_len;
        t[1] *= 1.f / t_len;
        t[2] *= 1.f / t_len;
        
        float b_len = sqrtf ( b[0] * b[0] + b[1] * b[1] + b[2] * b[2] );
        b[0] *= 1.f / b_len;
        b[1] *= 1.f / b_len;
        b[2] *= 1.f / b_len;
    }

    std_assert_m ( idx_count <= vertex_capacity * 6 );
    std_assert_m ( vert_count <= vertex_capacity );

    xg_geo_util_geometry_data_t result;
    result.pos = pos;
    result.nor = nor;
    result.uv = uv;
    result.tan = tan;
    result.bitan = bitan;
    result.idx = idx;
    result.vertex_count = vert_count;
    result.index_count = idx_count;
    return result;
}

xg_geo_util_geometry_data_t xg_geo_util_generate_plane ( float side ) {
    uint64_t vertex_capacity = 6;
    float* pos = std_virtual_heap_alloc_array_m ( float, vertex_capacity * 3 );
    float* nor = std_virtual_heap_alloc_array_m ( float, vertex_capacity * 3 );
    float* uv = std_virtual_heap_alloc_array_m ( float, vertex_capacity * 2 );
    float* tan = std_virtual_heap_alloc_array_m ( float, vertex_capacity * 3);
    float* bitan = std_virtual_heap_alloc_array_m ( float, vertex_capacity * 3);

    uint32_t* idx = std_virtual_heap_alloc_array_m ( uint32_t, 6 );

    uint32_t vert_it = 0;
    uint32_t idx_it = 0;

#define STORE_POS(x, y, z) pos[vert_it * 3 + 0] = x; pos[vert_it * 3 + 1] = y; pos[vert_it * 3 + 2] = z;
#define STORE_NOR(x, y, z) nor[vert_it * 3 + 0] = x; nor[vert_it * 3 + 1] = y; nor[vert_it * 3 + 2] = z;
#define STORE_UV(u, v)     uv [vert_it * 2 + 0] = u; uv [vert_it * 2 + 1] = v;
#define STORE_TAN(x, y, z) tan[vert_it * 3 + 0] = x; tan[vert_it * 3 + 1] = y; tan[vert_it * 3 + 2] = z;
#define STORE_BITAN(x, y, z) bitan[vert_it * 3 + 0] = x; bitan[vert_it * 3 + 1] = y; bitan[vert_it * 3 + 2] = z;
#define STORE_IDX(i) idx[idx_it] = i; ++idx_it;
#define STORE_VERTEX(x, y, z, u, v) STORE_POS(x, y, z); STORE_NOR(0, 1, 0); STORE_TAN(1, 0, 0); STORE_BITAN(0, 0, 1); STORE_UV(u, v); STORE_IDX(vert_it); ++vert_it;

    float h = -side / 2.f;

    // TODO uv
    STORE_VERTEX ( -h, 0, -h, 0, 0 );
    STORE_VERTEX ( -h, 0,  h, 0, 0 );
    STORE_VERTEX (  h, 0,  h, 0, 0 );

    STORE_VERTEX (  h, 0,  h, 0, 0 );
    STORE_VERTEX (  h, 0, -h, 0, 0 );
    STORE_VERTEX ( -h, 0, -h, 0, 0 );

#undef STORE_POS
#undef STORE_NOR
#undef STORE_UV
#undef STORE_IDX
#undef STORE_VERTEX

    xg_geo_util_geometry_data_t result;
    result.pos = pos;
    result.nor = nor;
    result.tan = tan;
    result.bitan = bitan;
    result.uv = uv;
    result.idx = idx;
    result.vertex_count = vert_it;
    result.index_count = idx_it;
    return result;
}

xg_geo_util_geometry_gpu_data_t xg_geo_util_upload_geometry_to_gpu ( xg_device_h device, const xg_geo_util_geometry_data_t* geo ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );

    xg_workload_h workload = xg->create_workload ( device );
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );

    xg_buffer_h pos_buffer = xg->cmd_create_buffer ( resource_cmd_buffer, &xg_buffer_params_m (
        .device = device,
        .size = sizeof ( float ) * geo->vertex_count * 3,
        .allowed_usage = xg_buffer_usage_bit_copy_dest_m | xg_buffer_usage_bit_vertex_buffer_m | xg_buffer_usage_bit_shader_device_address_m | xg_buffer_usage_bit_raytrace_geometry_buffer_m,
        .debug_name = "vbuffer_pos"
    ), &xg_buffer_init_m (
        .mode = xg_buffer_init_mode_upload_m,
        .upload_data = geo->pos,
    ) );

    xg_buffer_h nor_buffer = xg->cmd_create_buffer ( resource_cmd_buffer, &xg_buffer_params_m (
        .device = device,
        .size = sizeof ( float ) * geo->vertex_count * 3,
        .allowed_usage = xg_buffer_usage_bit_copy_dest_m | xg_buffer_usage_bit_vertex_buffer_m | xg_buffer_usage_bit_shader_device_address_m | xg_buffer_usage_bit_raytrace_geometry_buffer_m,
        .debug_name = "vbuffer_nor"
    ), &xg_buffer_init_m (
        .mode = xg_buffer_init_mode_upload_m,
        .upload_data = geo->nor,
    ) );

    xg_buffer_h tan_buffer = xg->cmd_create_buffer ( resource_cmd_buffer, &xg_buffer_params_m (
        .device = device,
        .size = sizeof ( float ) * geo->vertex_count * 3,
        .allowed_usage = xg_buffer_usage_bit_copy_dest_m | xg_buffer_usage_bit_vertex_buffer_m | xg_buffer_usage_bit_shader_device_address_m | xg_buffer_usage_bit_raytrace_geometry_buffer_m,
        .debug_name = "vbuffer_tan"
    ), &xg_buffer_init_m (
        .mode = xg_buffer_init_mode_upload_m,
        .upload_data = geo->tan,
    ) );

    xg_buffer_h bitan_buffer = xg->cmd_create_buffer ( resource_cmd_buffer, &xg_buffer_params_m (
        .device = device,
        .size = sizeof ( float ) * geo->vertex_count * 3,
        .allowed_usage = xg_buffer_usage_bit_copy_dest_m | xg_buffer_usage_bit_vertex_buffer_m | xg_buffer_usage_bit_shader_device_address_m | xg_buffer_usage_bit_raytrace_geometry_buffer_m,
        .debug_name = "vbuffer_bitan"
    ), &xg_buffer_init_m (
        .mode = xg_buffer_init_mode_upload_m,
        .upload_data = geo->bitan,
    ) );

    xg_buffer_h uv_buffer = xg->cmd_create_buffer ( resource_cmd_buffer, &xg_buffer_params_m (
        .device = device,
        .size = sizeof ( float ) * geo->vertex_count * 2,
        .allowed_usage = xg_buffer_usage_bit_copy_dest_m | xg_buffer_usage_bit_vertex_buffer_m | xg_buffer_usage_bit_shader_device_address_m | xg_buffer_usage_bit_raytrace_geometry_buffer_m,
        .debug_name = "vbuffer_uv"
    ), &xg_buffer_init_m (
        .mode = xg_buffer_init_mode_upload_m,
        .upload_data = geo->uv,
    ) );

    xg_buffer_h idx_buffer = xg->cmd_create_buffer ( resource_cmd_buffer, &xg_buffer_params_m (
        .device = device,
        .size = sizeof ( uint32_t ) * geo->index_count,
        .allowed_usage = xg_buffer_usage_bit_copy_dest_m | xg_buffer_usage_bit_index_buffer_m | xg_buffer_usage_bit_shader_device_address_m | xg_buffer_usage_bit_raytrace_geometry_buffer_m,
        .debug_name = "ibuffer"
    ), &xg_buffer_init_m (
        .mode = xg_buffer_init_mode_upload_m,
        .upload_data = geo->idx,
    ) );

    xg->submit_workload ( workload );

    xg_geo_util_geometry_gpu_data_t result = {
        .device = device,
        .pos_buffer = pos_buffer,
        .nor_buffer = nor_buffer,
        .tan_buffer = tan_buffer,
        .bitan_buffer = bitan_buffer,
        .uv_buffer = uv_buffer,
        .idx_buffer = idx_buffer,
    };
    return result;
}

void xg_geo_util_free_gpu_data ( xg_geo_util_geometry_gpu_data_t* gpu_data, xg_resource_cmd_buffer_h resource_cmd_buffer ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );
    
    xg->cmd_destroy_buffer ( resource_cmd_buffer, gpu_data->pos_buffer, xg_resource_cmd_buffer_time_workload_start_m );
    xg->cmd_destroy_buffer ( resource_cmd_buffer, gpu_data->nor_buffer, xg_resource_cmd_buffer_time_workload_start_m );
    xg->cmd_destroy_buffer ( resource_cmd_buffer, gpu_data->tan_buffer, xg_resource_cmd_buffer_time_workload_start_m );
    xg->cmd_destroy_buffer ( resource_cmd_buffer, gpu_data->bitan_buffer, xg_resource_cmd_buffer_time_workload_start_m );
    xg->cmd_destroy_buffer ( resource_cmd_buffer, gpu_data->idx_buffer, xg_resource_cmd_buffer_time_workload_start_m );
}

void xg_geo_util_free_data ( xg_geo_util_geometry_data_t* data ) {
    std_virtual_heap_free ( data->pos );
    std_virtual_heap_free ( data->nor );
    std_virtual_heap_free ( data->tan );
    std_virtual_heap_free ( data->bitan );
    std_virtual_heap_free ( data->uv );
    std_virtual_heap_free ( data->idx );
}
