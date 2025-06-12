#pragma once

#include <xg.h>
#include <se.h>

typedef enum {
    viewapp_scene_cornell_box_m,
    viewapp_scene_field_m,
    viewapp_scene_external_m,
} viewapp_scene_e;

void viewapp_load_scene ( viewapp_scene_e scene );

se_entity_h spawn_plane ( xg_workload_h workload );
se_entity_h spawn_sphere ( xg_workload_h workload );
se_entity_h spawn_light ( xg_workload_h workload );

void viewapp_destroy_entity_resources ( se_entity_h entity, xg_workload_h workload, xg_resource_cmd_buffer_h resource_cmd_buffer, xg_resource_cmd_buffer_time_e time );

void viewapp_build_raytrace_world ( xg_workload_h workload );
void update_raytrace_world ( void );
