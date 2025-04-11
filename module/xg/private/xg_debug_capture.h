#pragma once

#include <xg.h>

//typedef struct RENDERDOC_API_1_5_0 RENDERDOC_API_1_5_0;

typedef struct {
    void* renderdoc_api;
} xg_debug_capture_state_t;

void xg_debug_capture_load ( xg_debug_capture_state_t* state );
void xg_debug_capture_reload ( xg_debug_capture_state_t* state );
void xg_debug_capture_unload ( void );

void xg_debug_capture_start ( void );
void xg_debug_capture_stop ( void );

bool xg_debug_capture_is_available ( void );
