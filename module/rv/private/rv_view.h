#pragma once

#include <rv.h>

#include <std_allocator.h>
#include <std_mutex.h>

typedef struct {
    rv_view_transform_t transform;
    rv_matrix_4x4_t view_matrix;
    rv_matrix_4x4_t proj_matrix;
    rv_matrix_4x4_t jittered_proj_matrix;
    rv_matrix_4x4_t prev_frame_view_matrix;
    rv_matrix_4x4_t prev_frame_proj_matrix;
    uint32_t layer_mask;

    rv_view_params_t params;
} rv_view_t;

typedef struct {
    std_memory_h views_memory_handle;
    rv_view_t* views_array;
    rv_view_t* views_freelist;
    std_mutex_t views_mutex;
} rv_view_state_t;

void rv_view_load ( rv_view_state_t* state );
void rv_view_reload ( rv_view_state_t* state );
void rv_view_unload ( void );

void rv_view_init ( void );

rv_view_h rv_view_create ( const rv_view_params_t* params );
void rv_view_destroy ( rv_view_h view_handle );
void rv_view_get_info ( rv_view_info_t* info, rv_view_h view_handle );

void rv_view_update_transform ( rv_view_h view, const rv_view_transform_t* transform );
void rv_view_update_prev_frame_data ( rv_view_h view_handle );
void rv_view_update_jitter ( rv_view_h view_handle, uint64_t frame_id );
