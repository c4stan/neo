#pragma once

#include <xs.h>

typedef struct {
    xg_shading_stage_bit_e referenced_stages;
    char shaders[xg_shading_stage_count_m][xs_shader_name_max_len_m];
} xs_parser_shader_references_t;

typedef struct {
    uint32_t count;
    xs_shader_definition_t array[xs_shader_max_defines_m];
} xs_parser_shader_definitions_t;

// variations: permutations on actual pipeline state instead of just shader defines
// variations are not currently supported. One way to add support might be to split lexing and parsing,
//      building one stream of tokens per variation (including the base one). at that point the base stream
//      can be used to build the base state, and the variation streams can be applied on top of that to build
//      the variations.
/*
    xs example:
        ... base state ...
        begin variation reverse_z // to support reverse z depth write
            begin depth_stencil
                min_depth_bound 1
                max_depth_bound 0
                depth_test_op greater
            end
        end
        ... more base state ...
        begin variation reverse_z
            ... more stuff that ends up into the same variation ...
        end
*/

// permutations: are they needed? the main difference from permutation is the way you declare them,
//      variations are linearly declared one by one while perms are declared in "for loop" style.
//      the other is that classical perms only apply to defines and don't touch pipe state, but that's
//      arbitrary i believe.
//      but maybe the combinatory nature is not needed (yet?) and i can get away with just variations.
//      would be nice to have an idea of how 
/*
    xs example:
        TODO
*/

/*
    how to unify variations and permutations:
        in the xs check for flags set to certain values, and produce a variation stream for that case, e.g.

        if reverse_z == 1 // to support reverse z depth write
            begin depth_stencil
                min_depth_bound 1
                max_depth_bound 0
                depth_test_op greater
            end
        end
        ...

        this stream is to be applied to the final pipe state only if permutation flag reverse_z is set to 1 by the lookup, e.g.

        lookup_pipeline_state ( hash ( "pipeline_name" ), {
            { hash ( "reverse_z" ), 1 },
            { hash ( "..." ), ... },
            ...
        } )

        these same permutation flags can be checked as defines from shader code by adding them to the global define list when compiling the shader

    how to implement this:
        first to a lexing pass that splits all instructions into streams, one for the base state and another one for each 
        <permutation flag, value> pair found.
        Then compute the total number of permutations (multiply together the number of possible values each flag can take),
        iterate over that many elements, map each iteration number to a permutation and compile a pipeline state for it.
        When live editing a shader, all permutations for that shader should be recompiled, the same way as for the first compile pass.

    alternatives:
        - lazy compile states on lookup and cache the result instead of precompiling everything. this introduces possible lag on compile frames.
        - do not ever compile all permutations. instead, if a different permutation is wanted, a new file has to be manually created.
            the necessary features for this case would shrink down to being able to add shader defines from the xs file, to still be able to 
            drive some shader behavior from xs. 
*/

typedef struct {
    const char* path;
    xg_graphics_pipeline_params_t params;
    xs_parser_shader_references_t shader_references;
    xs_parser_shader_definitions_t shader_definitions;
} xs_parser_graphics_pipeline_state_t;

typedef struct {
    const char* path;
    xg_compute_pipeline_params_t params;
    xs_parser_shader_references_t shader_references;
    xs_parser_shader_definitions_t shader_definitions;
} xs_parser_compute_pipeline_state_t;

bool xs_parser_parse_graphics_pipeline_state_from_path ( xs_parser_graphics_pipeline_state_t* state, const char* path );

uint32_t xs_parser_parse_graphics_pipeline_variations_from_path ( xs_parser_graphics_pipeline_state_t* variations, const xs_parser_graphics_pipeline_state_t* base, const char* path );

bool xs_parser_parse_compute_pipeline_state_from_path ( xs_parser_compute_pipeline_state_t* state, const char* path );
