#include "rv_visible.h"

#include <std_list.h>

static rv_visible_state_t* rv_visible_state;

void rv_visible_load ( rv_visible_state_t* state ) {
    rv_visible_state = state;

    rv_visible_state->visible_array = std_virtual_heap_alloc_array_m ( rv_visible_t, rv_visible_max_visibles_m );
    rv_visible_state->visible_freelist = std_freelist_m ( rv_visible_state->visible_array, rv_visible_max_visibles_m );
    std_mutex_init ( &rv_visible_state->visible_mutex );
}

void rv_visible_reload ( rv_visible_state_t* state ) {
    rv_visible_state = state;
}

void rv_visible_unload ( void ) {
    std_virtual_heap_free ( rv_visible_state->visible_array );
    std_mutex_deinit ( &rv_visible_state->visible_mutex );
}

rv_visible_h rv_visible_create ( const rv_visible_params_t* params ) {
    std_mutex_lock ( &rv_visible_state->visible_mutex );
    rv_visible_t* visible = std_list_pop_m ( &rv_visible_state->visible_freelist );
    uint64_t idx = ( uint64_t ) ( visible - rv_visible_state->visible_array );
    std_bitset_set ( rv_visible_state->visible_bitset, idx );
    std_mutex_unlock ( &rv_visible_state->visible_mutex );

    visible->params = *params;

    std_auto_m handle = idx;
    return handle;
}

void destroy_visible ( rv_visible_h visible_handle ) {
    rv_visible_t* visible = &rv_visible_state->visible_array[visible_handle];

    std_mutex_lock ( &rv_visible_state->visible_mutex );
    std_list_push ( &rv_visible_state->visible_freelist, visible );
    std_bitset_clear ( rv_visible_state->visible_bitset, visible_handle );
    std_mutex_unlock ( &rv_visible_state->visible_mutex );
}

#if 0
size_t rv_visible_extract ( float* bounding_spheres, float* bounding_aabbs, float* orientations, uint32_t* layer_flags, uint64_t* render_flags, uint64_t** payloads ) {

}
#endif
