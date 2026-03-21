#include "rv_view.h"

#include <std_list.h>
#include <std_mutex.h>
#include <std_log.h>

#include <math.h>

#include <sm_matrix.h>
#include <sm_vector.h>
#include <sm_quat.h>

static rv_view_state_t* rv_view_state;

static void rv_view_view_matrix ( rv_matrix_4x4_t* m, const rv_view_transform_t* transform ) {
    sm_mat_4x4f_t view = sm_matrix_view (
        sm_vec_3f ( transform->position ),
        sm_quat ( transform->orientation )
    );

    for ( uint32_t i = 0; i < 16; ++i ) {
        m->f[i] = view.e[i];
    }
}

static void rv_view_orthographic_proj_matrix ( rv_matrix_4x4_t* m, const rv_orthographic_projection_params_t* params ) {
    sm_orthographic_projection_params_t sm_params = {
        .left = params->left,
        .right = params->right,
        .bottom = params->bottom,
        .top = params->top,
        .near_z = params->near_z,
        .far_z = params->far_z,
    };
    sm_mat_4x4f_t proj = sm_matrix_orthographic_proj ( &sm_params );

    for ( uint32_t i = 0; i < 16; ++i ) {
        m->f[i] = proj.e[i];
    }
}

static void rv_view_perspective_proj_matrix ( rv_matrix_4x4_t* m, const rv_perspective_projection_params_t* params ) {
    sm_perspective_projection_params_t sm_params = {
        .aspect_ratio = params->aspect_ratio,
        .near_z = params->near_z,
        .far_z = params->far_z,
        .fov_y = params->fov_y,
        .reverse_z = params->reverse_z,
        .infinite_far_z = params->infinite_far_z,
    };
    sm_mat_4x4f_t proj = sm_matrix_perspective_proj ( &sm_params );

    for ( uint32_t i = 0; i < 16; ++i ) {
        m->f[i] = proj.e[i];
    }
}

static void rv_view_jittered_perspective_proj_matrix ( rv_matrix_4x4_t* m, const rv_perspective_projection_params_t* params, uint64_t frame_id ) {
    rv_view_perspective_proj_matrix ( m, params );

    // jitter
    const float halton[16][2] = {
        {0.500000, 0.333333},
        {0.250000, 0.666667},
        {0.750000, 0.111111},
        {0.125000, 0.444444},
        {0.625000, 0.777778},
        {0.375000, 0.222222},
        {0.875000, 0.555556},
        {0.062500, 0.888889},
        {0.562500, 0.037037},
        {0.312500, 0.370370},
        {0.812500, 0.703704},
        {0.187500, 0.148148},
        {0.687500, 0.481481},
        {0.437500, 0.814815},
        {0.937500, 0.259259},
        {0.031250, 0.592593},
    };

    uint64_t idx = frame_id % 8;
    float offset_x = ( halton[idx][0] - 0.5f ) * 1 * params->jitter[0];
    float offset_y = ( halton[idx][1] - 0.5f ) * 1 * params->jitter[1];

    m->f[2] = offset_x;
    m->f[6] = offset_y;
}

void rv_view_init ( void ) {
    static rv_view_t views_array[rv_view_max_views_m];

    rv_view_state->views_array = views_array;
    rv_view_state->views_freelist = std_static_freelist_m ( views_array );
    std_mutex_init ( &rv_view_state->views_mutex );
}

void rv_view_load ( rv_view_state_t* state ) {
    rv_view_state = state;

    rv_view_state->views_array = std_virtual_heap_alloc_array_m ( rv_view_t, rv_view_max_views_m );
    rv_view_state->views_freelist = std_freelist_m ( rv_view_state->views_array, rv_view_max_views_m );
    std_mutex_init ( &rv_view_state->views_mutex );
}

void rv_view_unload ( void ) {
    std_virtual_heap_free ( rv_view_state->views_array );
    std_mutex_deinit ( &rv_view_state->views_mutex );
}

void rv_view_reload ( rv_view_state_t* state ) {
    rv_view_state = state;
}

rv_view_h rv_view_create ( const rv_view_params_t* params ) {
    std_mutex_lock ( &rv_view_state->views_mutex );
    rv_view_t* view = std_list_pop_m ( &rv_view_state->views_freelist );
    std_mutex_unlock ( &rv_view_state->views_mutex );

    view->params = *params;
    view->layer_mask = params->layer_mask;

    view->transform = params->transform;
    rv_view_view_matrix ( &view->view_matrix, &params->transform );
    if ( params->proj_params.type == rv_projection_perspective_m ) {
        rv_view_perspective_proj_matrix ( &view->proj_matrix, &params->proj_params.perspective );
        rv_view_jittered_perspective_proj_matrix ( &view->jittered_proj_matrix, &params->proj_params.perspective, 0 );
    } else {
        rv_view_orthographic_proj_matrix ( &view->proj_matrix, &params->proj_params.orthographic );
    }

    return ( rv_view_h ) ( view - rv_view_state->views_array );
}

void rv_view_destroy ( rv_view_h view_handle ) {
    std_mutex_lock ( &rv_view_state->views_mutex );
    std_list_push ( &rv_view_state->views_freelist, &rv_view_state->views_array[view_handle] );
    std_mutex_unlock ( &rv_view_state->views_mutex );
}

void rv_view_get_info ( rv_view_info_t* info, rv_view_h view_handle ) {
    rv_view_t* view = &rv_view_state->views_array[view_handle];

    info->layer_mask = view->layer_mask;
    info->view_type = view->params.view_type;
    info->transform = view->transform;
    info->view_matrix = view->view_matrix;
    info->proj_matrix = view->proj_matrix;
    info->jittered_proj_matrix = view->jittered_proj_matrix;
    info->prev_frame_view_matrix = view->prev_frame_view_matrix;
    info->prev_frame_proj_matrix = view->prev_frame_proj_matrix;
    info->proj_params = view->params.proj_params;

    // TODO cache these instead of computing them every time?
    // Inverse view matrix
    // it's the result of view_orientation^-1 * view_translation^-1
    {
        std_mem_zero_m ( &info->inverse_view_matrix );
        for ( uint32_t i = 0; i < 3; ++i ) {
            for ( uint32_t j = 0; j < 3; ++j ) {
                info->inverse_view_matrix.m[i][j] = info->view_matrix.m[j][i];
            }
        }

        sm_vec_3f_t pos = sm_vec_3f_set ( info->view_matrix.r0[3], info->view_matrix.r1[3], info->view_matrix.r2[3] );
        info->inverse_view_matrix.r0[3] = -sm_vec_3f_dot ( sm_vec_3f ( info->inverse_view_matrix.r0 ), pos );
        info->inverse_view_matrix.r1[3] = -sm_vec_3f_dot ( sm_vec_3f ( info->inverse_view_matrix.r1 ), pos );
        info->inverse_view_matrix.r2[3] = -sm_vec_3f_dot ( sm_vec_3f ( info->inverse_view_matrix.r2 ), pos );
        info->inverse_view_matrix.r3[3] = 1;
    }

    /*
    Projection matrix
        a    0    0    0
        0    b    0    0
        0    0    c    d
        0    0    e    0
    Inverse projection
        1/a, 0,   0,   0,
        0,   1/b, 0,   0,
        0,   0,   0,   1/e,
        0,   0,   1/d, -c/(d*e)
    */
    std_mem_zero_m ( &info->inverse_proj_matrix );
#if 1
    info->inverse_proj_matrix.f[0] = 1.f / info->proj_matrix.f[0];
    info->inverse_proj_matrix.f[5] = 1.f / info->proj_matrix.f[5];
    info->inverse_proj_matrix.f[11] = 1.f / info->proj_matrix.f[14];
    info->inverse_proj_matrix.f[14] = 1.f / info->proj_matrix.f[11];
    info->inverse_proj_matrix.f[15] = -info->proj_matrix.f[10] / ( info->proj_matrix.f[11] * info->proj_matrix.f[14] );
#else
    info->inverse_proj_matrix.f[0] = 1.f / info->jittered_proj_matrix.f[0];
    info->inverse_proj_matrix.f[5] = 1.f / info->jittered_proj_matrix.f[5];
    info->inverse_proj_matrix.f[11] = 1.f / info->jittered_proj_matrix.f[14];
    info->inverse_proj_matrix.f[14] = 1.f / info->jittered_proj_matrix.f[11];
    info->inverse_proj_matrix.f[15] = -info->jittered_proj_matrix.f[10] / ( info->jittered_proj_matrix.f[11] * info->jittered_proj_matrix.f[14] );
#endif

    // frustum planes
    sm_vec_4f_t frustum_planes[6];
    frustum_planes[rv_frustum_plane_left_m] = sm_vec_4f_add ( sm_vec_4f ( info->proj_matrix.r3 ), sm_vec_4f ( info->proj_matrix.r0 ) );
    frustum_planes[rv_frustum_plane_right_m] = sm_vec_4f_sub ( sm_vec_4f ( info->proj_matrix.r3 ), sm_vec_4f ( info->proj_matrix.r0 ) );
    frustum_planes[rv_frustum_plane_top_m] = sm_vec_4f_add ( sm_vec_4f ( info->proj_matrix.r3 ), sm_vec_4f ( info->proj_matrix.r1 ) );
    frustum_planes[rv_frustum_plane_bottom_m] = sm_vec_4f_sub ( sm_vec_4f ( info->proj_matrix.r3 ), sm_vec_4f ( info->proj_matrix.r1 ) );
    if ( view->params.proj_params.type == rv_projection_perspective_m && view->params.proj_params.perspective.reverse_z ) {
        frustum_planes[rv_frustum_plane_near_m] = sm_vec_4f_sub ( sm_vec_4f ( info->proj_matrix.r3 ), sm_vec_4f ( info->proj_matrix.r2 ) );
        frustum_planes[rv_frustum_plane_far_m] = sm_vec_4f ( info->proj_matrix.r2 );
    } else {
        frustum_planes[rv_frustum_plane_near_m] = sm_vec_4f ( info->proj_matrix.r2 );
        frustum_planes[rv_frustum_plane_far_m] = sm_vec_4f_sub ( sm_vec_4f ( info->proj_matrix.r3 ), sm_vec_4f ( info->proj_matrix.r2 ) );
    }
    for ( uint32_t i = 0; i < 6; ++i ) {
        float reciprocal_len = 1.f / sm_vec_3f_len ( sm_vec_4f_to_3f ( frustum_planes[i]) );
        frustum_planes[i] = sm_vec_4f_mul ( frustum_planes[i], reciprocal_len );
    }
    sm_vec_4f_store ( info->frustum_planes[rv_frustum_plane_left_m], frustum_planes[rv_frustum_plane_left_m] );
    sm_vec_4f_store ( info->frustum_planes[rv_frustum_plane_right_m], frustum_planes[rv_frustum_plane_right_m] );
    sm_vec_4f_store ( info->frustum_planes[rv_frustum_plane_top_m], frustum_planes[rv_frustum_plane_top_m] );
    sm_vec_4f_store ( info->frustum_planes[rv_frustum_plane_bottom_m], frustum_planes[rv_frustum_plane_bottom_m] );
    sm_vec_4f_store ( info->frustum_planes[rv_frustum_plane_near_m], frustum_planes[rv_frustum_plane_near_m] );
    sm_vec_4f_store ( info->frustum_planes[rv_frustum_plane_far_m], frustum_planes[rv_frustum_plane_far_m] );
}

void rv_view_update_transform ( rv_view_h view_handle, const rv_view_transform_t* transform ) {
    rv_view_t* view = &rv_view_state->views_array[view_handle];

    view->transform = *transform;
    rv_view_view_matrix ( &view->view_matrix, transform );
}

void rv_view_update_prev_frame_data ( rv_view_h view_handle ) {
    rv_view_t* view = &rv_view_state->views_array[view_handle];

    view->prev_frame_view_matrix = view->view_matrix;
    view->prev_frame_proj_matrix = view->proj_matrix;
}

void rv_view_update_jitter ( rv_view_h view_handle, uint64_t frame_id ) {
    rv_view_t* view = &rv_view_state->views_array[view_handle];

    std_assert_m ( view->params.proj_params.type == rv_projection_perspective_m );
    rv_view_jittered_perspective_proj_matrix ( &view->jittered_proj_matrix, &view->params.proj_params.perspective, frame_id );
}
