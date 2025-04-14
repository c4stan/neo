#include <lighting_pass.h>

#include <std_string.h>
#include <std_log.h>

#include <viewapp_state.h>

#include <xs.h>
#include <se.h>
#include <se.inl>

typedef struct {
    xs_database_pipeline_h pipeline_state;
    xg_sampler_h sampler;
    uint32_t width;
    uint32_t height;
} lighting_pass_args_t;

typedef enum {
    light_type_point_m,
    light_type_spot_m,
    light_type_directional_m,
} light_type_e;

typedef struct {
    float pos[3];
    float radius;
    float color[3];
    float intensity;
    rv_matrix_4x4_t proj_from_view;
    rv_matrix_4x4_t view_from_world;
    float shadow_tile[3]; // origin and size of the light shadow map in the shadow atlas
    uint32_t type;
} uniform_light_data_t;

typedef struct {
    uint32_t light_count;
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
    uniform_light_data_t lights[viewapp_max_lights_m];
} lighting_uniforms_t;

typedef struct {
    uint32_t light_count;
    uint32_t _pad0[3];
    uniform_light_data_t lights[];
} light_buffer_t;

//

static void light_update_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    se_i* se = state->modules.se;
    rv_i* rv = state->modules.rv;

    xg_buffer_h upload_buffer = node_args->io->copy_buffer_reads[0];
    xg_buffer_h light_buffer = node_args->io->copy_buffer_writes[0];

    xg_buffer_info_t upload_buffer_info;
    xg->get_buffer_info ( &upload_buffer_info, upload_buffer );

    se_query_result_t light_query_result;
    se->query_entities ( &light_query_result, &se_query_params_m ( 
        .component_count = 2, 
        .components = { viewapp_light_component_id_m, viewapp_transform_component_id_m } 
    ) );
    se_stream_iterator_t light_iterator = se_component_iterator_m ( &light_query_result.components[0], 0 );
    se_stream_iterator_t transform_iterator = se_component_iterator_m ( &light_query_result.components[1], 0 );
    uint64_t light_count = light_query_result.entity_count;
    light_count = std_min ( light_count, viewapp_max_lights_m );

    std_auto_m light_data = ( light_buffer_t* ) upload_buffer_info.allocation.mapped_address;
    std_assert_m ( light_data );

    uint32_t light_view_count = 0;

    for ( uint64_t light_it = 0; light_it < light_count; ++light_it ) {
        viewapp_light_component_t* light_component = se_stream_iterator_next ( &light_iterator );
        viewapp_transform_component_t* transform_component = se_stream_iterator_next ( &transform_iterator );

        for ( uint32_t view_it = 0; view_it < light_component->view_count; ++view_it) {
            rv_view_info_t view_info;
            rv->get_view_info ( &view_info, light_component->views[view_it] );

            light_data->lights[light_view_count++] = ( uniform_light_data_t ) {
                .pos =  {
                    transform_component->position[0],
                    transform_component->position[1],
                    transform_component->position[2],
                },
                .radius = light_component->radius,
                .color = { 
                    light_component->color[0],
                    light_component->color[1],
                    light_component->color[2],
                },
                .intensity = light_component->intensity,
                .proj_from_view = view_info.proj_matrix,
                .view_from_world = view_info.view_matrix,
                .shadow_tile = {
                    light_component->shadow_tiles[view_it].x,
                    light_component->shadow_tiles[view_it].y,
                    light_component->shadow_tiles[view_it].size
                },
            };
        }
    }

    // Null light (radius == 0) at the end needed by the light cull pass
    light_data->lights[light_view_count] = ( uniform_light_data_t ) { 0 };

    light_data->light_count = light_view_count;
    std_assert_m ( light_view_count <= viewapp_max_lights_m );

    xg->cmd_copy_buffer ( node_args->cmd_buffer, node_args->base_key, &xg_buffer_copy_params_m ( .source = upload_buffer, .destination = light_buffer ) );
}

xf_node_h add_light_update_pass ( xf_graph_h graph, xf_buffer_h upload_buffer, xf_buffer_h light_buffer ) {
    viewapp_state_t* state = viewapp_state_get();
    xf_i* xf = state->modules.xf;

    xf_node_h node = xf->add_node ( graph, &xf_node_params_m ( 
        .type = xf_node_type_custom_pass_m,
        .debug_name = "light_update",
        .pass.custom = xf_node_custom_pass_params_m (
            .routine = light_update_pass,
        ),
        .resources = xf_node_resource_params_m (
            .copy_buffer_reads_count = 1,
            .copy_buffer_reads = { upload_buffer },
            .copy_buffer_writes_count = 1,
            .copy_buffer_writes = { light_buffer },
        )
    ) );
    return node;
}

uint32_t uniform_light_size ( void ) {
    return sizeof ( uniform_light_data_t );
}

void get_light_uniform_scale_bias ( float* scale, float* bias ) {

}
