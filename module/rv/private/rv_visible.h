#pragma once

#include <rv.h>

#include <std_allocator.h>
#include <std_mutex.h>

typedef struct {
    rv_visible_params_t params;
} rv_visible_t;

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
