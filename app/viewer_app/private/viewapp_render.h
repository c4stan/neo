#pragma once

#include <xg.h>
#include <xs.h>

#include "viewapp_scene.h"

void viewapp_boot_render ( void );

void viewapp_unload_render ( xg_workload_h workload, xg_resource_cmd_buffer_h resource_cmd_buffer );

typedef enum {
    viewapp_render_graph_raster_m,
    viewapp_render_graph_restir_di_m,
    viewapp_render_graph_raytrace_m,
    viewapp_render_graph_count_m,
    viewapp_render_graph_invalid_m,
} viewapp_render_graph_e;

void viewapp_boot_workload_resources_layout ( void );
void viewapp_update_workload_uniforms ( xg_workload_h workload );

void viewapp_load_render_graph ( viewapp_render_graph_e graph, xg_workload_h workload );
void viewapp_load_mouse_pick_graph ( void );
void viewapp_load_ibl_cubemap_gen_graph ( void );

void viewapp_destroy_render_graph ( xg_workload_h workload, xg_resource_cmd_buffer_h resource_cmd_buffer );
void viewapp_destroy_mouse_pick_graph ( xg_workload_h workload, xg_resource_cmd_buffer_h resource_cmd_buffer );
void viewapp_destroy_ibl_cubemap_gen_graph ( xg_workload_h workload, xg_resource_cmd_buffer_h resource_cmd_buffer );

void viewapp_reload_graphs ( void );

bool viewapp_render_graph_is_raytrace ( viewapp_render_graph_e graph );
xs_database_pipeline_h viewapp_get_render_graph_raytrace_pipeline ( viewapp_render_graph_e graph );

uint64_t viewapp_render_update_sky ( viewapp_sky_component_t* sky_component, xg_workload_h workload, xg_resource_cmd_buffer_h resource_cmd_buffer, uint64_t key );
