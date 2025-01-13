#include <geometry.h>

#include <std_log.h>

#include <math.h>

#if 0
static xg_buffer_h upload_rt_vertex_buffer_to_gpu ( xg_device_h device, xg_cmd_buffer_h cmd_buffer, xg_resource_cmd_buffer_h resource_cmd_buffer, const geometry_data_t* data ) {
    float* pos = data->pos;
    float* nor = data->nor;
    uint32_t vertex_count = data->vertex_count;

    xg_i* xg = std_module_get_m ( xg_module_name_m );

    xg_buffer_h staging = xg->create_buffer ( &xg_buffer_params_m (
        .memory_type = xg_memory_type_upload_m,
        .device = device,
        .size = vertex_count * sizeof ( float ) * 6,
        .allowed_usage = xg_buffer_usage_bit_copy_source_m,
        .debug_name = "staging temp buffer",
    ) );

    xg_buffer_info_t staging_info;
    xg->get_buffer_info ( &staging_info, staging );
    float* buffer_data = ( float* ) staging_info.allocation.mapped_address;

    for ( uint32_t i = 0; i < vertex_count; ++i ) {
        buffer_data[i * 6 + 0] = pos[i * 3 + 0];
        buffer_data[i * 6 + 1] = pos[i * 3 + 1];
        buffer_data[i * 6 + 2] = pos[i * 3 + 2];
        buffer_data[i * 6 + 3] = nor[i * 3 + 0];
        buffer_data[i * 6 + 4] = nor[i * 3 + 1];
        buffer_data[i * 6 + 5] = nor[i * 3 + 2];
    }

    xg_buffer_h buffer = xg->cmd_create_buffer ( resource_cmd_buffer, &xg_buffer_params_m (
        .memory_type = xg_memory_type_gpu_only_m,
        .device = device,
        .size = vertex_count * sizeof ( float ) * 6,
        .allowed_usage = xg_buffer_usage_bit_copy_dest_m | xg_buffer_usage_bit_raytrace_geometry_buffer_m | xg_buffer_usage_bit_shader_device_address_m,
        .debug_name = "vbuffer rt stream",
    ) );

    xg->cmd_copy_buffer ( cmd_buffer, staging, buffer, 0 );
    xg->cmd_destroy_buffer ( resource_cmd_buffer, staging, xg_resource_cmd_buffer_time_workload_complete_m );
    return buffer;
}
#endif

// http://www.songho.ca/opengl/gl_sphere.html
geometry_data_t generate_sphere ( float rad, uint32_t meridians_count, uint32_t parallels_count ) {
    float pi = 3.1415f;
    float meridian_step = 2.f * pi / meridians_count;
    float parallel_step = pi / parallels_count;

    // vbuffer
    uint64_t vertex_capacity = ( parallels_count + 1 ) * ( meridians_count + 1 );
    float* pos = std_virtual_heap_alloc_array_m ( float, vertex_capacity * 3 );
    float* nor = std_virtual_heap_alloc_array_m ( float, vertex_capacity * 3 );
    //std_alloc_t tan_alloc = std_virtual_heap_alloc ( sizeof ( float ) * vertex_capacity * 3, 16 );
    //float* pos = ( float* ) pos_alloc.buffer.base;
    //float* nor = ( float* ) nor_alloc.buffer.base;
    //float* tang = ( float* ) tan_alloc.buffer.base;
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

            pos[vert_count * 3 + 0] = x;
            pos[vert_count * 3 + 1] = y;
            pos[vert_count * 3 + 2] = z;
            nor[vert_count * 3 + 0] = nx;
            nor[vert_count * 3 + 1] = ny;
            nor[vert_count * 3 + 2] = nz;
            ++vert_count;
        }
    }

    // ibuffer
    uint32_t* idx = std_virtual_heap_alloc_array_m ( uint32_t, parallels_count * meridians_count * 6 );
    //uint32_t* idx =  ( uint32_t* ) idx_alloc.buffer.base;
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

    std_assert_m ( idx_count <= vertex_capacity * 6 );
    std_assert_m ( vert_count <= vertex_capacity );

    geometry_data_t result;
    result.pos = pos;
    result.nor = nor;
    result.idx = idx;
    result.vertex_count = vert_count;
    result.index_count = idx_count;
    return result;
}

geometry_data_t generate_plane ( float side ) {
    uint64_t vertex_capacity = 6;
    float* pos = std_virtual_heap_alloc_array_m ( float, vertex_capacity * 3 );
    float* nor = std_virtual_heap_alloc_array_m ( float, vertex_capacity * 3 );

    uint32_t* idx = std_virtual_heap_alloc_array_m ( uint32_t, 6 );

    uint32_t vert_it = 0;
    uint32_t idx_it = 0;

#define STORE_POS(x, y, z) pos[vert_it * 3 + 0] = x; pos[vert_it * 3 + 1] = y; pos[vert_it * 3 + 2] = z;
#define STORE_NOR(x, y, z) nor[vert_it * 3 + 0] = x; nor[vert_it * 3 + 1] = y; nor[vert_it * 3 + 2] = z;
#define STORE_IDX(i) idx[idx_it] = i; ++idx_it;
#define STORE_VERTEX(x, y, z) STORE_POS(x, y, z); STORE_NOR(0, 1, 0); STORE_IDX(vert_it); ++vert_it;

    float h = -side / 2.f;

    STORE_VERTEX ( -h, 0, -h );
    STORE_VERTEX ( -h, 0,  h );
    STORE_VERTEX (  h, 0,  h );

    STORE_VERTEX (  h, 0,  h );
    STORE_VERTEX (  h, 0, -h );
    STORE_VERTEX ( -h, 0, -h );

#undef STORE_POS
#undef STORE_NOR
#undef STORE_IDX
#undef STORE_VERTEX

    geometry_data_t result;
    result.pos = pos;
    result.nor = nor;
    result.idx = idx;
    result.vertex_count = vert_it;
    result.index_count = idx_it;
    return result;
}

geometry_gpu_data_t upload_geometry_to_gpu ( xg_device_h device, const geometry_data_t* geo ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );

    xg_buffer_params_t pos_staging_params = xg_buffer_params_m (
        .memory_type = xg_memory_type_upload_m,
        .device = device,
        .size = geo->vertex_count * sizeof ( float ) * 3,
        .allowed_usage = xg_buffer_usage_bit_copy_source_m,
        .debug_name = "staging temp buffer",
    );
    xg_buffer_h pos_staging = xg->create_buffer ( &pos_staging_params );

    xg_buffer_params_t nor_staging_params = xg_buffer_params_m (
        .memory_type = xg_memory_type_upload_m,
        .device = device,
        .size = geo->vertex_count * sizeof ( float ) * 3,
        .allowed_usage = xg_buffer_usage_bit_copy_source_m,
        .debug_name = "staging temp buffer",
    );
    xg_buffer_h nor_staging = xg->create_buffer ( &nor_staging_params );

    xg_buffer_params_t idx_staging_params = xg_buffer_params_m (
        .memory_type = xg_memory_type_upload_m,
        .device = device,
        .size = geo->index_count * sizeof ( uint32_t ),
        .allowed_usage = xg_buffer_usage_bit_copy_source_m,
        .debug_name = "staging temp buffer",
    );
    xg_buffer_h idx_staging = xg->create_buffer ( &idx_staging_params );

    xg_buffer_info_t pos_staging_info;
    xg->get_buffer_info ( &pos_staging_info, pos_staging );
    float* pos_buffer_data = ( float* ) pos_staging_info.allocation.mapped_address;

    xg_buffer_info_t nor_staging_info;
    xg->get_buffer_info ( &nor_staging_info, nor_staging );
    float* nor_buffer_data = ( float* ) nor_staging_info.allocation.mapped_address;

    xg_buffer_info_t idx_staging_info;
    xg->get_buffer_info ( &idx_staging_info, idx_staging );
    uint32_t* idx_buffer_data = ( uint32_t* ) idx_staging_info.allocation.mapped_address;

    for ( uint64_t i = 0; i < geo->vertex_count; ++i ) {
        pos_buffer_data[i * 3 + 0] = geo->pos[i * 3 + 0];
        pos_buffer_data[i * 3 + 1] = geo->pos[i * 3 + 1];
        pos_buffer_data[i * 3 + 2] = geo->pos[i * 3 + 2];

        nor_buffer_data[i * 3 + 0] = geo->nor[i * 3 + 0];
        nor_buffer_data[i * 3 + 1] = geo->nor[i * 3 + 1];
        nor_buffer_data[i * 3 + 2] = geo->nor[i * 3 + 2];
    }

    for ( uint64_t i = 0; i < geo->index_count; ++i ) {
        idx_buffer_data[i] = geo->idx[i];
    }

    xg_workload_h workload = xg->create_workload ( device );
    xg_cmd_buffer_h cmd_buffer = xg->create_cmd_buffer ( workload );
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );

    // TODO make separate temporary buffers for pos and idx with the additional xg_buffer_usage_bit_raytrace_geometry_buffer_m flag? what's the additional cost?
    xg_buffer_h pos_buffer = xg->cmd_create_buffer ( resource_cmd_buffer, &xg_buffer_params_m (
        .memory_type = xg_memory_type_gpu_only_m,
        .device = device,
        .size = geo->vertex_count * sizeof ( float ) * 3,
        .allowed_usage = xg_buffer_usage_bit_copy_dest_m | xg_buffer_usage_bit_vertex_buffer_m | xg_buffer_usage_bit_shader_device_address_m | xg_buffer_usage_bit_raytrace_geometry_buffer_m,
        .debug_name = "vbuffer pos stream",
    ) );

    xg_buffer_h nor_buffer = xg->cmd_create_buffer ( resource_cmd_buffer, &xg_buffer_params_m (
        .memory_type = xg_memory_type_gpu_only_m,
        .device = device,
        .size = geo->vertex_count * sizeof ( float ) * 3,
        .allowed_usage = xg_buffer_usage_bit_copy_dest_m | xg_buffer_usage_bit_vertex_buffer_m | xg_buffer_usage_bit_shader_device_address_m,
        .debug_name = "vbuffer nor stream",
    ) );

    xg_buffer_h idx_buffer = xg->cmd_create_buffer ( resource_cmd_buffer, &xg_buffer_params_m (
        .memory_type = xg_memory_type_gpu_only_m,
        .device = device,
        .size = geo->index_count * sizeof ( uint32_t ),
        .allowed_usage = xg_buffer_usage_bit_copy_dest_m | xg_buffer_usage_bit_index_buffer_m | xg_buffer_usage_bit_shader_device_address_m | xg_buffer_usage_bit_raytrace_geometry_buffer_m,
        .debug_name = "ibuffer",
    ) );

    xg->cmd_copy_buffer ( cmd_buffer, 0, &xg_buffer_copy_params_m ( .source = nor_staging, .destination = nor_buffer ) );
    xg->cmd_copy_buffer ( cmd_buffer, 0, &xg_buffer_copy_params_m ( .source = pos_staging, .destination = pos_buffer ) );
    xg->cmd_copy_buffer ( cmd_buffer, 0, &xg_buffer_copy_params_m ( .source = idx_staging, .destination = idx_buffer ) );

    xg->cmd_destroy_buffer ( resource_cmd_buffer, pos_staging, xg_resource_cmd_buffer_time_workload_complete_m );
    xg->cmd_destroy_buffer ( resource_cmd_buffer, nor_staging, xg_resource_cmd_buffer_time_workload_complete_m );
    xg->cmd_destroy_buffer ( resource_cmd_buffer, idx_staging, xg_resource_cmd_buffer_time_workload_complete_m );

    //xg_buffer_h rt_buffer = upload_rt_vertex_buffer_to_gpu ( device, cmd_buffer, resource_cmd_buffer, geo );

    xg->submit_workload ( workload );

    geometry_gpu_data_t result;
    result.device = device;
    result.pos_buffer = pos_buffer;
    result.nor_buffer = nor_buffer;
    result.idx_buffer = idx_buffer;
    //result.rt_buffer = rt_buffer;
    return result;
}

void free_gpu_data ( geometry_gpu_data_t* gpu_data ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );

    xg_workload_h workload = xg->create_workload ( gpu_data->device );
    xg_resource_cmd_buffer_h xg_resource_cmd_buffer_h = xg->create_resource_cmd_buffer ( workload );
    
    xg->cmd_destroy_buffer ( xg_resource_cmd_buffer_h, gpu_data->pos_buffer, xg_resource_cmd_buffer_time_workload_start_m );
    xg->cmd_destroy_buffer ( xg_resource_cmd_buffer_h, gpu_data->nor_buffer, xg_resource_cmd_buffer_time_workload_start_m );
    xg->cmd_destroy_buffer ( xg_resource_cmd_buffer_h, gpu_data->idx_buffer, xg_resource_cmd_buffer_time_workload_start_m );
    
    xg->submit_workload ( workload );
}
