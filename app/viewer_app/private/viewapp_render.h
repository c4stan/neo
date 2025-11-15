#pragma once

#include <xg.h>

typedef enum {
    viewapp_render_graph_raster_m,
    viewapp_render_graph_restir_di_m,
    viewapp_render_graph_count_m,
} viewapp_render_graph_e;

void viewapp_boot_workload_resources_layout ( void );
void viewapp_update_workload_uniforms ( xg_workload_h workload );

void viewapp_load_render_graph ( viewapp_render_graph_e graph, xg_workload_h workload );
void viewapp_load_mouse_pick_graph ( void );

void viewapp_reload_graphs ( void );

bool viewapp_is_raytrace_world_used ( void );
