#pragma once

#include <wm.h>

#include "wm_window.h"

typedef struct {
    wm_i api;
    std_memory_h memory_handle;
    wm_window_state_t window;
} wm_state_t;

wm_state_t* wm_state_alloc ( void );
void wm_state_free ( void );
void wm_state_bind ( wm_state_t* state );
