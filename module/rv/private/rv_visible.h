#pragma once

#include <rv.h>

#include <std_allocator.h>
#include <std_mutex.h>

typedef struct {
    rv_visible_params_t params;
} rv_visible_t;

#if 0
typedef struct {
    float rotation[4];
    float aabb_min[3];
    float aabb_max[3];
    float scale[3];
} rv_visible_cell_transform_t;

typedef struct {
    float pos_rad[4];
} rv_visible_cell_sphere_t;

typedef struct {
    rv_visible_cell_sphere_t spheres;
    rv_visible_h visibles;
    rv_visible_cell_transform_t transforms;
} rv_visible_cell_page_t;

#define rv_visible_max_pages_per_cell_m 32

typedef struct {
    rv_visible_cell_page_t* pages[rv_visible_max_pages_per_cell_m];
    uint64_t pages_count;
} rv_visible_cell_t;

typedef struct {
    rv_visible_bvh_node_t* nodes;
    uint64_t nodes_count;
} rv_visible_bvh_t;
#endif

typedef struct {
    rv_visible_t* visible_array;
    rv_visible_t* visible_freelist;
    uint64_t* visible_bitset;
    std_mutex_t visible_mutex;
} rv_visible_state_t;

void rv_visible_load ( rv_visible_state_t* state );
void rv_visible_reload ( rv_visible_state_t* state );
void rv_visible_unload ( void );

rv_visible_h rv_visible_create ( const rv_visible_params_t* params );
void rv_visible_destroy ( rv_visible_h visible );

#if 0
    size_t rv_visible_extract ( float* bounding_spheres, float* bounding_aabbs, float* orientations, uint32_t* layer_flags, uint64_t* render_flags, uint64_t** payloads );
#endif
