#pragma once

#include <xg.h>

typedef struct {
    float* pos;
    float* nor;
    float* tan;
    float* bitan;
    float* uv;
    uint32_t* idx;
    uint64_t vertex_count;
    uint64_t index_count;
} xg_geo_util_geometry_data_t;

typedef struct {
    xg_device_h device;
    xg_buffer_h pos_buffer;
    xg_buffer_h nor_buffer;
    xg_buffer_h tan_buffer;
    xg_buffer_h bitan_buffer;
    xg_buffer_h uv_buffer;
    xg_buffer_h idx_buffer;
} xg_geo_util_geometry_gpu_data_t;

xg_geo_util_geometry_data_t xg_geo_util_generate_sphere ( float rad, uint32_t meridians_count, uint32_t parallels_count );
xg_geo_util_geometry_data_t xg_geo_util_generate_plane ( float side );

xg_geo_util_geometry_gpu_data_t xg_geo_util_upload_geometry_to_gpu ( xg_device_h device, xg_workload_h workload, const xg_geo_util_geometry_data_t* geo );
void xg_geo_util_free_gpu_data ( xg_geo_util_geometry_gpu_data_t* gpu_data, xg_workload_h workload, xg_resource_cmd_buffer_time_e time );
void xg_geo_util_free_data ( xg_geo_util_geometry_data_t* data );
