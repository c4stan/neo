#include "wm_state.h"

#include "wm_window.h"

#include <std_log.h>

static wm_state_t* wm_state;

wm_state_t* wm_state_alloc ( void ) {
    std_assert_m ( !wm_state );
    std_alloc_t state_alloc = std_virtual_heap_alloc ( sizeof ( wm_state_t ), std_alignof_m ( wm_state_t ) );
    wm_state_t* state = ( wm_state_t* ) state_alloc.buffer.base;
    state->memory_handle = state_alloc.handle;
    wm_state = state;
    return state;
}

void wm_state_free ( void ) {
    std_assert_m ( wm_state );
    std_virtual_heap_free ( wm_state->memory_handle );
    wm_state = NULL;
}

void wm_state_bind ( wm_state_t* state ) {
    wm_state = state;
}
