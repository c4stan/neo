#pragma once

#include <xg.h>

typedef struct {
    float* pos;
    float* nor;
    //float* tang; // :(
    uint32_t* idx;
    uint64_t vertex_count;
    uint64_t index_count;
} geometry_data_t;

typedef struct {
    xg_buffer_h pos_buffer;
    xg_buffer_h nor_buffer;
    xg_buffer_h idx_buffer;
} geometry_gpu_data_t;

geometry_data_t generate_sphere ( float rad, uint32_t meridians_count, uint32_t parallels_count );
geometry_data_t generate_plane ( float side );

geometry_gpu_data_t upload_geometry_to_gpu ( xg_device_h device, const geometry_data_t* geo );
