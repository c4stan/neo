#pragma once

#include <wm.h>
#include <xg.h>
#include <xs.h>
#include <xf.h>

typedef struct {
    uint32_t resolution_x;
    uint32_t resolution_y;
    uint32_t frame_id;
    bool capture_frame;

    wm_window_h window;

    xg_device_h device;
    xg_swapchain_h swapchain;
    xg_texture_h depth_stencil;

    xf_graph_h render_graph;

    std_tick_t frame_tick;
    wm_window_info_t window_info;
    wm_input_state_t input_state;

    xg_i* xg;
    xs_i* xs;
    xf_i* xf;
} viewapp_renderer_state_t;

void viewapp_renderer_load ( viewapp_renderer_state_t* state );
void viewapp_renderer_reload ( viewapp_renderer_state_t* state );
void viewapp_renderer_unload ( void );

bool viewapp_renderer_update ( void );
