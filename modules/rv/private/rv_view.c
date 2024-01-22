#include "rv_view.h"

#include <std_list.h>
#include <std_mutex.h>
#include <std_log.h>

#include <math.h>

static rv_view_state_t* rv_view_state;

static void rv_view_crossf3 ( float* dest, const float* a, const float* b ) {
    dest[0] = a[1] * b[2] - a[2] * b[1];
    dest[1] = a[2] * b[0] - a[0] * b[2];
    dest[2] = a[0] * b[1] - a[1] * b[0];
}

static float rv_view_dotf3 ( const float* a, const float* b ) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void rv_view_normf3 ( float* dest, const float* a ) {
    float len = sqrtf ( rv_view_dotf3 ( a, a ) );
    dest[0] = a[0] / len;
    dest[1] = a[1] / len;
    dest[2] = a[2] / len;
}

#if 0
static void rv_view_rotation_matrix_from_quaternion ( rv_matrix_4x4_t* m, float* q ) {
    float x2 = q[0] * q[0];
    float y2 = q[1] * q[1];
    float z2 = q[2] * q[2];
    float xy;
    float xz;
    float yz;
    float wx;
    float wy;
    float wz;

    m->r0[0] = 1.f - 2.f * ( y2 + z2 );
    m->r0[1] = 2.f * ( xy - wz );
    m->r0[2] = 2.f * ( xz + wy );

    m->r1[0] = 2.f * ( xy + wz );
    m->r1[1] = 1.f - 2.f * ( x2 + z2 );
    m->r1[2] = 2.f * ( yz - wx );

    m->r2[0] = 2.f * ( xz - wy );
    m->r2[1] = 2.f * ( yz + wx );
    m->r2[2] = 1.f - 2.f * ( x2 + y2 );
}
#endif

static void rv_view_view_matrix ( rv_matrix_4x4_t* m, const float* pos, const float* focus ) {
    /*
        https://www.3dgep.com/understanding-the-view-matrix/
        view = orientation^-1 * translation^-1
            orientation^-1: just transpose the orthonormalized camera basis matrix (orientation)
            translation^-1: just fill the translation column with the negated camera position
        the below is the above, but in one single step. (also need to recompute basis vectors)
    */

    std_mem_zero_m ( m );

    float up[3] = { 0.f, 1.f, 0.f };
    float dir[3] = { focus[0] - pos[0], focus[1] - pos[1], focus[2] - pos[2] };

#if 1
    // z axis
    rv_view_normf3 ( m->r2, dir );

    if ( m->r2[0] == 0 && m->r2[1] == -1 && m->r2[2] == 0 ) {
        up[0] = 0;
        up[1] = 0;
        up[2] = 1;
    }

    float t[3];
    // x axis
    rv_view_crossf3 ( t, up, m->r2 );
    rv_view_normf3 ( m->r0, t );
    // y axis
    rv_view_crossf3 ( m->r1, m->r2, m->r0 );
#else
    float x_axis[3], y_axis[3], z_axis[3];
    rv_view_normf3 ( z_axis, dir );
    rv_view_crossf3 ( x_axis, up, z_axis );
    rv_view_crossf3 ( y_axis, z_axis, x_axis );
#endif

#if 1
    m->r0[3] = -rv_view_dotf3 ( m->r0, pos );
    m->r1[3] = -rv_view_dotf3 ( m->r1, pos );
    m->r2[3] = -rv_view_dotf3 ( m->r2, pos );
    m->r3[3] = 1;
#else
    m->r0[0] = x_axis[0];
    m->r0[1] = y_axis[0];
    m->r0[2] = z_axis[0];
    m->r0[3] = 0;

    m->r1[0] = x_axis[1];
    m->r1[1] = y_axis[1];
    m->r1[2] = z_axis[1];
    m->r1[3] = 0;

    m->r2[0] = x_axis[2];
    m->r2[1] = y_axis[2];
    m->r2[2] = z_axis[2];
    m->r2[3] = 0;

    m->r3[0] = -rv_view_dotf3 ( x_axis, pos );
    m->r3[1] = -rv_view_dotf3 ( y_axis, pos );
    m->r3[2] = -rv_view_dotf3 ( z_axis, pos );
    m->r3[3] = 1;
#endif
}

static void rv_view_perspective_proj_matrix ( rv_matrix_4x4_t* m, const rv_projection_params_t* params ) {
    /*
        http://www.songho.ca/opengl/gl_projectionmatrix.html
        https://matthewwellings.com/blog/the-new-vulkan-coordinate-system/
        Premise:
            Eye space is left handed. (Camera towards positive Z axis)
    -- X, Y --
        eye -> near plane (xe,ye -> xp,yp)
            <x,y,z>e somewhere inside the frustum
            xp in [-w/2, w/2], yp in  [-h/2, h/2]
            [from similar triangles formula: xp/xe == znear/ze]
            xp = znear * xe / ze
            yp = znear * ye / ze
            *division by ze happens automatically in hw later*
        near plane -> NDC (xp, yp -> xn, yn)
            xp in [-w/2, w/2], yp in  [-h/2, h/2]
            xn in [-1, 1], yn in [-1, 1]
            [simple remapping]
            xn = xp / w * 2
            yn = yp / h * 2
        putting it together, eye -> NDC
            xn = 2 * znear / w * xe = znear / (w/2) * xe
            yn = 2 * znear / h * ye = znear / (h/2) * ye
    -- Z --
        eye -> NDC (ze -> zn)
            ze in [znear, zfar]
            zn in [0, 1]
            zn = (A*ze + B) / ze
            [the target remapping rewritten as system]
            | (A*znear + B) / znear = 0
            | (A*zfar + B) / zfar = 1
            [solving for A, B]
            B = -A*znear -> (A*zfar - A*znear) / zfar = 1 -> A = zfar / (zfar - znear)
            B = -(zfar * znear) / (zfar - znear)
        for reversed Z:
            | (A*znear + B) / znear = 1
            | (A*zfar + B) / zfar = 0
            [solving for A, B]
            B = -A*zfar -> (A*znear - A*zfar) / znear = 1 -> A = znear / (znear - zfar)
            B = -(zfar * znear) / (znear - zfar)
    */

    std_assert_m ( params->type == rv_projection_perspective_m );

    float near_z = params->near_z;
    float far_z = params->far_z;
    float half_h = near_z * tanf ( params->fov_y / 2.f );
    float half_w = params->aspect_ratio * half_h;

#if 1
#if 1
    // Row 0
    m->f[0] = near_z / half_w;
    m->f[1] = 0;
    m->f[2] = 0;
    m->f[3] = 0;

    // Row 1
    m->f[4] = 0;
    m->f[5] = near_z / half_h;
    m->f[6] = 0;
    m->f[7] = 0;

    // Row 2
    m->f[8] = 0;
    m->f[9] = 0;
    m->f[10] = far_z / ( far_z - near_z );
    m->f[11] = - ( far_z * near_z ) / ( far_z - near_z );
    //m->f[10] = near_z / ( near_z - far_z );
    //m->f[11] = - ( far_z * near_z ) / ( near_z - far_z );

    // Row 3
    m->f[12] = 0;
    m->f[13] = 0;
    m->f[14] = 1;
    m->f[15] = 0;
#else
    // Row 0
    m->f[0] = near_z / half_w;
    m->f[1] = 0;
    m->f[2] = 0;
    m->f[3] = 0;

    // Row 1
    m->f[4] = 0;
    m->f[5] = - ( near_z / half_h );
    m->f[6] = 0;
    m->f[7] = 0;

    // Row 2
    m->f[8] = 0;
    m->f[9] = 0;
    m->f[10] = near_z / ( far_z - near_z );
    m->f[11] = ( far_z * near_z ) / ( far_z - near_z );

    // Row 3
    m->f[12] = 0;
    m->f[13] = 0;
    m->f[14] = -1;
    m->f[15] = 0;
#endif
#else
    m->r0[0] = near_z / half_w;
    m->r0[1] = 0;
    m->r0[2] = 0;
    m->r0[3] = 0;

    m->r1[0] = 0;
    m->r1[1] = near_z / half_h;
    m->r1[2] = 0;
    m->r1[3] = 0;

    m->r2[0] = 0;
    m->r2[1] = 0;
    m->r2[2] = far_z / ( far_z - near_z );
    m->r2[3] = 1;

    m->r3[0] = 0;
    m->r3[1] = 0;
    m->r3[2] = - ( far_z * near_z ) / ( far_z - near_z );
    m->r3[3] = 0;
#endif
}

static void rv_view_jittered_perspective_proj_matrix ( rv_matrix_4x4_t* m, const rv_projection_params_t* params, uint64_t frame_id ) {
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
    float offset_x = ( halton[idx][0] - 0.5f ) * 2 * params->jitter[0];
    float offset_y = ( halton[idx][1] - 0.5f ) * 2 * params->jitter[1];

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

    std_alloc_t views_alloc = std_virtual_heap_alloc_array_m ( rv_view_t, rv_view_max_views_m );
    rv_view_state->views_memory_handle = views_alloc.handle;
    rv_view_state->views_array = ( rv_view_t* ) views_alloc.buffer.base;
    rv_view_state->views_freelist = std_freelist ( views_alloc.buffer, sizeof ( rv_view_t ) );
    std_mutex_init ( &rv_view_state->views_mutex );
}

void rv_view_unload ( void ) {
    std_virtual_heap_free ( rv_view_state->views_memory_handle );
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

    view->transform.position[0] = params->position[0];
    view->transform.position[1] = params->position[1];
    view->transform.position[2] = params->position[2];
    view->transform.focus_point[0] = params->focus_point[0];
    view->transform.focus_point[1] = params->focus_point[1];
    view->transform.focus_point[2] = params->focus_point[2];

    rv_view_view_matrix ( &view->view_matrix, params->position, params->focus_point );
    rv_view_perspective_proj_matrix ( &view->proj_matrix, &params->proj_params );
    rv_view_jittered_perspective_proj_matrix ( &view->jittered_proj_matrix, &params->proj_params, 0 );

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
        for ( uint32_t i = 0; i < 3; ++i ) {
            for ( uint32_t j = 0; j < 3; ++j ) {
                info->inverse_view_matrix.m[i][j] = info->view_matrix.m[j][i];
            }
        }

        float pos[3];
        pos[0] = info->view_matrix.r0[3];
        pos[1] = info->view_matrix.r1[3];
        pos[2] = info->view_matrix.r2[3];

        info->inverse_view_matrix.r0[3] = -rv_view_dotf3 ( info->inverse_view_matrix.r0, pos );
        info->inverse_view_matrix.r1[3] = -rv_view_dotf3 ( info->inverse_view_matrix.r1, pos );
        info->inverse_view_matrix.r2[3] = -rv_view_dotf3 ( info->inverse_view_matrix.r2, pos );
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
    info->inverse_proj_matrix.f[0] = 1.f / info->proj_matrix.f[0];
    info->inverse_proj_matrix.f[5] = 1.f / info->proj_matrix.f[5];
    info->inverse_proj_matrix.f[11] = 1.f / info->proj_matrix.f[14];
    info->inverse_proj_matrix.f[14] = 1.f / info->proj_matrix.f[11];
    info->inverse_proj_matrix.f[15] = -info->proj_matrix.f[10] / ( info->proj_matrix.f[11] * info->proj_matrix.f[14] );
}

void rv_view_update_transform ( rv_view_h view_handle, const rv_view_transform_t* transform ) {
    rv_view_t* view = &rv_view_state->views_array[view_handle];

    view->transform.position[0] = transform->position[0];
    view->transform.position[1] = transform->position[1];
    view->transform.position[2] = transform->position[2];
    view->transform.focus_point[0] = transform->focus_point[0];
    view->transform.focus_point[1] = transform->focus_point[1];
    view->transform.focus_point[2] = transform->focus_point[2];
    rv_view_view_matrix ( &view->view_matrix, transform->position, transform->focus_point );
}

void rv_view_update_prev_frame_data ( rv_view_h view_handle ) {
    rv_view_t* view = &rv_view_state->views_array[view_handle];

    view->prev_frame_view_matrix = view->view_matrix;
    view->prev_frame_proj_matrix = view->proj_matrix;
}

void rv_view_update_jitter ( rv_view_h view_handle, uint64_t frame_id ) {
    rv_view_t* view = &rv_view_state->views_array[view_handle];

    rv_view_jittered_perspective_proj_matrix ( &view->jittered_proj_matrix, &view->params.proj_params, frame_id );
}

#if 0

static void quat_mul ( float* q, const float* q1, const float* q2 ) {
    q[0] = q1[0] * q2[3] + q1[1] * q2[2] - q1[2] * q2[1] + q1[3] * q2[0];
    q[1] = q1[1] * q2[3] + q1[2] * q2[0] + q1[3] * q2[1] - q1[0] * q2[2];
    q[2] = q1[2] * q2[3] + q1[3] * q2[2] + q1[0] * q2[1] - q1[1] * q2[0];
    q[3] = q1[3] * q2[3] - q1[0] * q2[0] - q1[1] * q2[1] - q1[2] * q2[2];
}

static void quat_conj ( float* c, const float* q ) {
    c[0] = -q[0];
    c[1] = -q[1];
    c[2] = -q[2];
    c[3] = q[3];
}

void rv_view_update_transform ( rv_view_h view_handle, const rv_view_transform_t* transform ) {
    rv_view_t* view = &rv_view_state->views_array[view_handle];

    // get matrix from <pos,rot> xform
    float conjugate_orientation[] = {
        -transform->orientation[0],
        -transform->orientation[1],
        -transform->orientation[2],
        transform->orientation[3]
    };

    float inverse_orientation[] = {

    }
}

#endif
