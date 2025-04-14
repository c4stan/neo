#pragma once

#include <std_module.h>

#define rv_module_name_m rv
std_module_export_m void* rv_load ( void* );
std_module_export_m void rv_reload ( void*, void* );
std_module_export_m void rv_unload ( void );

typedef uint64_t rv_view_h;
typedef uint64_t rv_entity_h;
typedef uint64_t rv_query_h;

#define rv_null_handle_m UINT64_MAX

#define rv_deg_to_rad_m 0.0174533f

typedef enum {
    rv_projection_perspective_m,
    rv_projection_orthographic_m
} rv_projection_type_e;

// align required by freelist
std_static_align_m ( 8 )
typedef struct {
    rv_projection_type_e type;
    float aspect_ratio;
    float near_z;
    float far_z;
    // TODO use fov_x instead?
    float fov_y; // in radians
    float jitter[2];
    bool reverse_z;         // todo
    bool infinite_far_z;    // todo
} rv_perspective_projection_params_t;

#define rv_perspective_projection_params_m( ... ) ( rv_perspective_projection_params_t ) { \
    .type = rv_projection_perspective_m, \
    .aspect_ratio = ( 16.f / 9.f ), \
    .near_z = 0.1f, \
    .far_z = 1000.f, \
    .fov_y = 90 * rv_deg_to_rad_m, \
    .jitter = { 0, 0 }, \
    .reverse_z = false, \
    .infinite_far_z = false, \
    ##__VA_ARGS__ \
}

std_static_align_m ( 8 )
typedef struct {
    rv_projection_type_e type;
    float left;
    float right;
    float bottom;
    float top;
    float near_z;
    float far_z;
} rv_orthographic_projection_params_t;

#define rv_orthographic_projection_params_m( ... ) ( rv_orthographic_projection_params_t ) { \
    .type = rv_projection_orthographic_m, \
    .left = 0, \
    .right = 0, \
    .bottom = 0, \
    .top = 0, \
    .near = 0, \
    .far = 0, \
    ##__VA_ARGS__ \
}

typedef struct {
    union {
        rv_projection_type_e type;
        rv_perspective_projection_params_t perspective;
        rv_orthographic_projection_params_t orthographic;
    };
} rv_projection_params_t;

// TODO just take in resolution and compute aspect ration and jitter from that
#define rv_projection_params_m(...) ( rv_projection_params_t ) { \
    .perspective = rv_perspective_projection_params_m(), \
    ##__VA_ARGS__ \
}

/*
// TODO replace with something like <position, orientation> ?
typedef struct {
    float position[3];
    float direction[3]
    float up[3];
} rv_view_params_t;
*/

// Row major storage
// r0 = m[0] = e00 e01 e02 e03
// r1 = m[1] = e04 e05 e06 e07
// r2 = m[2] = e08 e09 e10 e11
// r3 = m[3] = e12 e13 e14 e15
typedef union {
    float f[16];
    float m[4][4];
    struct {
        float r0[4];
        float r1[4];
        float r2[4];
        float r3[4];
    };
} rv_matrix_4x4_t;

typedef struct {
    float position[3];    // x y z
    float orientation[4]; // xi yj zk w
    //float focus_point[3];
} rv_view_transform_t;

#define rv_view_transform_m( ... ) ( rv_view_transform_t ) { \
    .position = { 0, 0, 0 }, \
    .orientation = { 0, 0, 0, 1 }, \
    ##__VA_ARGS__ \
}

typedef struct {
    //rv_view_params_t view_params;
    //rv_view_transform_t initial_transform;
    //float position[3];
    //float focus_point[3];
    //float orientation[4];
    rv_view_transform_t transform;
    rv_projection_params_t proj_params;
    uint32_t view_type;     // main view, shadow view, ... | TODO make it a bitfield instead of an enum?
    uint32_t layer_mask;    // bitmask of layers the view will render
} rv_view_params_t;

#define rv_view_params_m(...) ( rv_view_params_t ) { \
    .transform = rv_view_transform_m(), \
    .proj_params = rv_projection_params_m(), \
    .view_type = 0, \
    .layer_mask = 0, \
    ##__VA_ARGS__ \
}

typedef struct {
    rv_view_h view;
    //uint32_t view_type;
    uint64_t renderable_mask;
} rv_query_params_t;

// TODO store float[16] instead of matrices ?
typedef struct {
    uint32_t layer_mask;
    uint32_t view_type;
    rv_view_transform_t transform;
    rv_matrix_4x4_t view_matrix;
    rv_matrix_4x4_t proj_matrix;
    rv_matrix_4x4_t jittered_proj_matrix;
    rv_matrix_4x4_t inverse_view_matrix;
    rv_matrix_4x4_t inverse_proj_matrix;
    rv_matrix_4x4_t prev_frame_view_matrix;
    rv_matrix_4x4_t prev_frame_proj_matrix;
    rv_projection_params_t proj_params;
} rv_view_info_t;

typedef struct {
    float sphere[4];        // center xyz, rad
    float aabb[6];          // min xyz, max xyz
    float orientation[4];   // quaternion
} rv_bounding_volumes_t;

typedef struct {
    rv_bounding_volumes_t bounding_volumes;
    uint32_t layer_flags;
    uint64_t render_flags;
    uint64_t payload;
} rv_entity_params_t;

typedef struct {
    uint64_t count;
    uint64_t* payloads;
} rv_query_results_t;

// TODO: how to handle static vs dynamic entities?

typedef struct {
    rv_view_h ( *create_view ) ( const rv_view_params_t* params );
    void ( *destroy_view ) ( rv_view_h view );
    void ( *get_view_info ) ( rv_view_info_t* info, rv_view_h view );

    // TODO unify these updates?
    void ( *update_view_transform ) ( rv_view_h view, const rv_view_transform_t* transform );
    void ( *update_prev_frame_data ) ( rv_view_h view );
    void ( *update_proj_jitter ) ( rv_view_h view, uint64_t frame_id );

    rv_entity_h ( *create_entity ) ( const rv_entity_params_t* params );
    void ( *destroy_entity ) ( rv_entity_h entity );
#if 0
    bool ( *update_visibles_layer_flags ) ( const rv_visible_h* visibles, uint32_t* flags, size_t count );
    bool ( *update_visibles_render_flags ) ( const rv_visible_h* visibles, uint64_t* flags, size_t count );
    bool ( *update_visibles_bounding_volumes ) ( const rv_visible_h* visibles, rv_bounding_volumes_t* volumes, size_t count );
#endif
} rv_i;
