#pragma once

#include <xs.h>

typedef struct {
    xg_shading_stage_bit_e referenced_stages;
    char shaders[xg_shading_stage_count_m][xs_shader_name_max_len_m];
} xs_parser_shader_references_t;

/*

    lookup_pipeline_state ( hash("pipeline_name"), {
        { hash("permutation0"_, value0 },
        { hash("permutation1"), value1 },
    })

    database:
        pipeline state:
            table: permutation name hash -> permutation idx, bits

        lookup_pipeline_state:
            use table and input to create permutation u32, use to lookup actual compiled pso object

        build_shaders:
            build table by assigning idxs from parser declaration order

*/

#if 0
typedef struct {
    uint32_t id;
    uint32_t define_count;
    xs_shader_definition_t defines[xs_shader_permutation_max_defines_m];
} xs_parser_shader_permutation_t;
#else
//std_static_assert_m ( xs_shader_definition_name_max_len_m > sizeof ( "permutation_" ) + sizeof ( "_m" ) )
#define xs_shader_permutation_name_max_len_m (xs_shader_definition_name_max_len_m - sizeof ( "permutation_" ) - sizeof ( "_m" ))

typedef struct {
    xg_shading_stage_bit_e stages;
    uint32_t value_count;
    char name[xs_shader_permutation_name_max_len_m];
} xs_parser_shader_permutation_t;
#endif

typedef struct {
    uint32_t permutation_count;
    xs_parser_shader_permutation_t permutations[xs_shader_max_permutations_m];
} xs_parser_shader_permutations_t;

typedef struct {
    const char* path;
    xg_graphics_pipeline_params_t params;
    xs_parser_shader_references_t shader_references;
    xs_parser_shader_permutations_t shader_permutations;
} xs_parser_graphics_pipeline_state_t;

typedef struct {
    const char* path;
    xg_compute_pipeline_params_t params;
    xs_parser_shader_references_t shader_references;
    // TODO replace permutations with array of defines
    xs_parser_shader_permutations_t shader_permutations;
} xs_parser_compute_pipeline_state_t;

typedef struct {
    uint32_t variations_count;
    xs_parser_graphics_pipeline_state_t variations[xs_shader_max_variations_m];
} xs_parser_graphics_pipeline_variations_t;

bool xs_parser_parse_graphics_pipeline_state_from_path ( xs_parser_graphics_pipeline_state_t* state, const char* path );

uint32_t xs_parser_parse_graphics_pipeline_variations_from_path ( xs_parser_graphics_pipeline_state_t* variations, const xs_parser_graphics_pipeline_state_t* base, const char* path );

bool xs_parser_parse_compute_pipeline_state_from_path ( xs_parser_compute_pipeline_state_t* state, const char* path );
