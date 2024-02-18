#include "xs_database.h"

#include "xs_shader_compiler.h"
#include "xs_parser.h"

#include <std_allocator.h>
#include <std_list.h>
#include <std_string.h>
#include <std_log.h>

#include <fs.h>

static xs_database_state_t* xs_database_state;

void xs_database_load ( xs_database_state_t* state ) {
    xs_database_state = state;

    for ( size_t i = 0; i < xs_database_max_memory_pages_m; ++i ) {
        xs_database_memory_page_t* page = &xs_database_state->memory_pages[i];

        page->alloc = std_null_alloc_m;
        page->top = 0;
    }

    xs_database_state->folders_count = 0;
    xs_database_state->pipeline_states_count = 0;

    std_assert_m ( sizeof ( xs_string_hash_t ) <= sizeof ( uint64_t ) );
    std_assert_m ( sizeof ( xg_graphics_pipeline_state_h ) <= sizeof ( uint64_t ) );
    static uint64_t pipeline_name_hash_array[xs_database_max_pipeline_states_m * 2];
    static uint64_t pipeline_name_hash_payload_array[xs_database_max_pipeline_states_m * 2];
    //static uint64_t pipeline_handle_array[xs_database_max_pipeline_states_m * 2];
    //static uint64_t pipeline_handle_payload_array[xs_database_max_pipeline_states_m * 2];

    xs_database_state->pipeline_name_hash_to_state_map = std_hash_map ( std_static_buffer_m ( pipeline_name_hash_array ), std_static_buffer_m ( pipeline_name_hash_payload_array ) );
    //xs_database_state.pipeline_handle_to_state_map = std_hash_map ( std_static_buffer_m ( pipeline_handle_array ), std_static_buffer_m ( pipeline_handle_payload_array ) );

    xs_database_state->output_path[0] = '\0';
    xs_database_state->pipeline_state_headers_last_build_timestamp = std_timestamp_zero_m;
}

void xs_database_reload ( xs_database_state_t* state ) {
    xs_database_state = state;
}

void xs_database_unload ( void ) {
    // TODO
}

static char* xs_database_alloc_string ( size_t size ) {
    xs_database_memory_page_t* memory_page = NULL;

    for ( size_t i = 0; i < xs_database_max_memory_pages_m; ++i ) {
        xs_database_memory_page_t* page = &xs_database_state->memory_pages[i];

        if ( page->alloc.handle.id == std_null_memory_handle_id_m ) {
            page->alloc = std_virtual_alloc ( std_virtual_page_size() );
            page->top = 0;
            std_assert_m ( page->alloc.handle.id != std_null_memory_handle_id_m );
            memory_page = page;
            break;
        } else if ( page->alloc.buffer.size - page->top < size ) {
            continue;
        } else {
            memory_page = page;
        }
    }

    std_assert_m ( memory_page != NULL );

    char* dest = ( char* ) memory_page->alloc.buffer.base + memory_page->top;
    memory_page->top += size;
    return dest;
}

typedef struct {
    fs_i* fs;
    char* base;
} xs_database_folder_iterator_params_t;

static void xs_database_folder_iterator ( const char* name, fs_path_flags_t flags, void* arg ) {
    if ( ! ( flags & fs_path_is_file_m ) ) {
        return;
    }

    xs_database_folder_iterator_params_t* params = ( xs_database_folder_iterator_params_t* ) arg;

    size_t path_len = params->fs->append_path ( params->base, fs_path_size_m, name );
    bool is_header = false;
    xg_pipeline_e type;
    {
        size_t extension_base = std_str_find_reverse ( params->base, path_len, "." );

        if ( extension_base == std_str_find_null_m ) {
            params->fs->pop_path ( params->base );
            return;
        }

        // TODO remove .xss
        if ( std_str_cmp ( params->base + extension_base, ".xsg" ) == 0 || std_str_cmp ( params->base + extension_base, ".xss" ) == 0 ) {
            type = xg_pipeline_graphics_m;
        } else if ( std_str_cmp ( params->base + extension_base, ".xsc" ) == 0 ) {
            type = xg_pipeline_compute_m;
        } else if ( std_str_cmp ( params->base + extension_base, ".glsl" ) == 0 ) {
            is_header = true;
        } else {
            params->fs->pop_path ( params->base );
            return;
        }
    }

    char* dest = xs_database_alloc_string ( path_len + 1 );
    std_str_copy ( dest, path_len + 1, params->base );

    if ( is_header ) {
        xs_database_pipeline_state_header_t* header = &xs_database_state->pipeline_state_headers[xs_database_state->pipeline_state_headers_count++];
        header->path = dest;
    } else {
        xs_database_pipeline_state_t* pipeline_state = &xs_database_state->pipeline_states[xs_database_state->pipeline_states_count++];
        pipeline_state->path = dest;
        pipeline_state->name = params->fs->get_path_name_ptr ( dest );
        pipeline_state->type = type;
        pipeline_state->pipeline_handle = xg_null_handle_m;
        pipeline_state->old_pipeline_handle = xg_null_handle_m;
        pipeline_state->old_pipeline_workload = xg_null_handle_m;
        pipeline_state->last_build_timestamp = std_timestamp_zero_m;

        size_t name_len = std_str_len ( pipeline_state->name );
        name_len = std_str_find_reverse ( pipeline_state->name, name_len, "." );
        std_assert_m ( name_len != std_str_find_null_m );
        xs_string_hash_t hash = xs_hash_string_m ( pipeline_state->name, name_len );

        pipeline_state->name_hash = hash;

        // TODO insert here when the shader is built
        bool unique = std_hash_map_insert ( &xs_database_state->pipeline_name_hash_to_state_map, hash, ( uint64_t ) pipeline_state );
        std_assert_m ( unique );
    }

    params->fs->pop_path ( params->base );
}

bool xs_database_add_folder ( const char* input_path ) {
    fs_i* fs = std_module_get_m ( fs_module_name_m );

    char path[fs_path_size_m] = { 0 };
    size_t path_len = fs->get_absolute_path ( path, fs_path_size_m, input_path );

    // TODO check that the path exists and is a folder

    char* dest = xs_database_alloc_string ( path_len + 1 );
    std_str_copy ( dest, path_len + 1, path );
    xs_database_state->folders[xs_database_state->folders_count++] = dest;

    xs_database_folder_iterator_params_t args;
    args.fs = fs;
    args.base = path;
    fs->iterate_dir ( path, xs_database_folder_iterator, &args );

    return true;
}

bool xs_database_set_output_folder ( const char* input_path ) {
    fs_i* fs = std_module_get_m ( fs_module_name_m );

    fs_path_info_t info;
    fs->get_path_info ( &info, input_path );
    bool result = info.flags & fs_path_is_dir_m;

    if ( !result ) {
        result = fs->create_dir ( input_path );
    }

    char path[fs_path_size_m] = { 0 };
    fs->get_absolute_path ( path, fs_path_size_m, input_path );

    std_str_copy ( xs_database_state->output_path, fs_path_size_m, path );

    return result;
}

void xs_database_clear ( void ) {
    for ( size_t i = 0; i < xs_database_max_memory_pages_m; ++i ) {
        xs_database_memory_page_t* page = &xs_database_state->memory_pages[i];

        if ( page->alloc.handle.id != std_null_memory_handle_id_m ) {
            std_virtual_free ( page->alloc.handle );
            page->alloc = std_null_alloc_m;
            page->top = 0;
        } else {
            break;
        }
    }

    xs_database_state->folders_count = 0;
    xs_database_state->pipeline_states_count = 0;
}

xs_database_build_result_t xs_database_build_shaders ( xg_device_h device, const xs_database_build_params_t* build_params ) {
    xs_database_build_result_t result;
    result.successful_shaders = 0;
    result.failed_shaders = 0;
    result.skipped_shaders = 0;
    result.successful_pipeline_states = 0;
    result.failed_pipeline_states = 0;

    fs_i* fs = std_module_get_m ( fs_module_name_m );
    xg_i* xg = std_module_get_m ( xg_module_name_m );

    std_log_info_m ( "Building shader database..." );

    // check headers, froce rebuild all shaders if one changed
    // TODO try to take dependencies into account and avoid rebuilding everything?
    // TODO account for xs.glsl changes
    bool dirty_headers = false;
    std_timestamp_t most_recent_header_edit = std_timestamp_zero_m;

    for ( size_t header_it = 0; header_it < xs_database_state->pipeline_state_headers_count && !dirty_headers; ++header_it ) {
        xs_database_pipeline_state_header_t* header = &xs_database_state->pipeline_state_headers[header_it];

        fs_file_info_t info;
        std_assert_m ( fs->get_file_path_info ( &info, header->path ) );

        dirty_headers |= xs_database_state->pipeline_state_headers_last_build_timestamp.count < info.last_write_time.count;
    
        if ( info.last_write_time.count > most_recent_header_edit.count ) {
            most_recent_header_edit = info.last_write_time;
        }
    }

    if ( dirty_headers ) {
        xs_database_state->pipeline_state_headers_last_build_timestamp = std_timestamp_now_utc();
    }

    // check pipeline states
    for ( size_t state_it = 0; state_it < xs_database_state->pipeline_states_count; ++state_it ) {
        xs_database_pipeline_state_t* pipeline_state = &xs_database_state->pipeline_states[state_it];

        fs_file_h pipeline_state_file = fs->open_file ( pipeline_state->path, fs_file_read_m );
        std_assert_m ( pipeline_state_file != fs_null_handle_m );

        fs_file_info_t pipeline_state_file_info;
        std_assert_m ( fs->get_file_info ( &pipeline_state_file_info, pipeline_state_file ) );

        fs->close_file ( pipeline_state_file );

        bool state_parse_result = false;

        typedef union {
            xs_parser_graphics_pipeline_state_t graphics;
            xs_parser_compute_pipeline_state_t compute;
        } xs_parsed_pipeline_state_t;

        xs_parsed_pipeline_state_t parsed_pipeline_state;
        std_mem_zero_m ( &parsed_pipeline_state );

        xs_parser_shader_references_t* shader_references;
        xs_parser_shader_permutations_t* shader_permutations;

        switch ( pipeline_state->type ) {
            case xg_pipeline_graphics_m:
                parsed_pipeline_state.graphics.params = xg_default_graphics_pipeline_params_m;
                parsed_pipeline_state.graphics.params.debug_name = pipeline_state->name;

                // Initialize viewport size with param values, the xss can override it if desired
                parsed_pipeline_state.graphics.params.state.viewport_state.width = ( uint32_t ) build_params->viewport_width;
                parsed_pipeline_state.graphics.params.state.viewport_state.height = ( uint32_t ) build_params->viewport_height;

                state_parse_result = xs_parser_parse_graphics_pipeline_state_from_path ( &parsed_pipeline_state.graphics, pipeline_state->path );

                shader_references = &parsed_pipeline_state.graphics.shader_references;
                shader_permutations = &parsed_pipeline_state.graphics.shader_permutations;

                break;

            case xg_pipeline_compute_m:
                parsed_pipeline_state.compute.params = xg_default_compute_pipeline_params_m;
                parsed_pipeline_state.compute.params.debug_name = pipeline_state->name;

                state_parse_result = xs_parser_parse_compute_pipeline_state_from_path ( &parsed_pipeline_state.compute, pipeline_state->path );

                shader_references = &parsed_pipeline_state.compute.shader_references;
                shader_permutations = &parsed_pipeline_state.compute.shader_permutations;

                break;
        }

        // TODO heap alloc this, too big for the stack
        uint32_t variation_count = 0;
        union {
            xs_parser_graphics_pipeline_state_t graphics[xs_shader_max_variations_m];
            xs_parser_compute_pipeline_state_t compute[xs_shader_max_variations_m];
        } parsed_pipeline_variations;
        std_mem_zero_m ( &parsed_pipeline_variations );

        switch ( pipeline_state->type ) {
            case xg_pipeline_graphics_m:
                //variation_count = xs_parser_parse_graphics_pipeline_variations_from_path ( parsed_pipeline_variations.graphics, &parsed_pipeline_state.graphics, pipeline_state->path );
                break;

            case xg_pipeline_compute_m:
                //std_not_implemented_m();
                break;
        }

        if ( !state_parse_result ) {
            ++result.failed_pipeline_states;
            continue;
        }

        char input_path[fs_path_size_m];
        std_str_copy ( input_path, fs_path_size_m, pipeline_state->path );
        fs->pop_path ( input_path );

        char output_path[fs_path_size_m];

        if ( xs_database_state->output_path[0] ) {
            std_str_copy ( output_path, fs_path_size_m, xs_database_state->output_path );
        } else {
            std_str_copy ( output_path, fs_path_size_m, input_path );
        }

        struct {
            std_memory_h handle;
            std_buffer_t buffer;
        } shader_bytecode[xg_shading_stage_count_m];

        for ( size_t i = 0; i < xg_shading_stage_count_m; ++i ) {
            shader_bytecode[i].handle = std_null_memory_handle_m;
            shader_bytecode[i].buffer = std_null_buffer_m;
        }

        char shader_path[fs_path_size_m];
        char binary_path[fs_path_size_m];

        // check if the pipeline state needs to be (re)built
        // TODO move further up, no need to parse the file if it doesn't need to be rebuilt?
        bool needs_to_build = dirty_headers || pipeline_state->last_build_timestamp.count < pipeline_state_file_info.last_write_time.count;

        if ( !needs_to_build ) {
            for ( xg_shading_stage_e stage = 0; stage < xg_shading_stage_count_m; ++stage ) {
                if ( shader_references->referenced_stages & xg_shading_stage_enum_to_bit_m ( stage ) ) {
                    std_str_copy ( shader_path, fs_path_size_m, input_path );
                    fs->append_path ( shader_path, fs_path_size_m, shader_references->shaders[stage] );

                    // check shader source edit time
                    fs_file_info_t shader_source_info;
                    std_assert_m ( fs->get_file_path_info ( &shader_source_info, shader_path ) );

                    if ( pipeline_state->last_build_timestamp.count < shader_source_info.last_write_time.count ) {
                        needs_to_build = true;
                        break;
                    }
                }
            }
        }

        if ( !needs_to_build ) {
            continue;
        }

        // TODO all of the following up to the bump to successful builds count should go into a for loop that iterates over variations (including the base variation)
        for ( uint32_t variation_it = 0; variation_it < variation_count + 1; ++variation_it ) {
            xs_parser_graphics_pipeline_state_t* graphics_state;
            xs_parser_compute_pipeline_state_t* compute_state;

            if ( variation_it == 0 ) {
                graphics_state = &parsed_pipeline_state.graphics;
                compute_state = &parsed_pipeline_state.compute;
            } else {
                if ( pipeline_state->type == xg_pipeline_graphics_m ) {
                    graphics_state = &parsed_pipeline_variations.graphics[variation_it - 1];
                    compute_state = &parsed_pipeline_variations.compute[variation_it - 1];
                } else {
                    graphics_state = &parsed_pipeline_variations.graphics[variation_it - 1];
                    compute_state = &parsed_pipeline_variations.compute[variation_it - 1];
                }
            }

            size_t shader_success = 0;
            size_t shader_fail = 0;
            size_t shader_skip = 0;

            for ( xg_shading_stage_e stage = 0; stage < xg_shading_stage_count_m; ++stage ) {
                if ( shader_references->referenced_stages & xg_shading_stage_enum_to_bit_m ( stage ) ) {

                    // TODO compile all shader permutations here
                    //xs_parser_shader_permutation_t* permutation = &shader_permutations->permutations[permutation_it];

                    // TODO
                    // generate permutations
                    //std_stack_t stack;
                    //bool default_permutation = false;

                    //if ( shader_permutations->permutation_count > 0 ) {
                    //    std_assert_m ( 1 << shader_permutations->permutation_count < 1024 );
                    //    uint32_t permutation_buffer[1024];
                    //    stack = std_stack ( std_static_buffer_m ( permutation_buffer ) );
                    //} else {
                    //    default_permutation = true;
                    //}

                    // TODO iterate over permutations, adjust output file name based on permutation id, pass proper defines to compiler,
                    //      store each permutation separetely in the db and combine pipeline name and permutation hashes

                    // prepare shader code input path
                    std_str_copy ( shader_path, fs_path_size_m, input_path );
                    fs->append_path ( shader_path, fs_path_size_m, shader_references->shaders[stage] );

                    // prepare shader bytecode output path
                    std_str_copy ( binary_path, fs_path_size_m, output_path );
                    fs->append_path ( binary_path, fs_path_size_m, shader_references->shaders[stage] );
                    size_t len = std_str_len ( binary_path );
                    size_t len2 = std_str_find_reverse ( binary_path, len, "." );

                    if ( len2 != std_str_find_null_m ) {
                        len = len2;
                    }

                    std_array_t array = std_static_array_m ( binary_path );
                    array.count = len;

                    const char* stage_tag = "";

                    if ( stage == xg_shading_stage_vertex_m ) {
                        stage_tag = "vs";
                    } else if ( stage == xg_shading_stage_fragment_m ) {
                        stage_tag = "fs";
                    } else if ( stage == xg_shading_stage_compute_m ) {
                        stage_tag = "cs";
                    }

                    std_str_append ( binary_path, &array, "-" );
                    std_str_append ( binary_path, &array, stage_tag );
                    std_str_append ( binary_path, &array, ".spv" );

                    // check if shader needs to be (re)compiled
                    bool skip_compile = false;
                    fs_file_info_t shader_source_info;
                    std_assert_m ( fs->get_file_path_info ( &shader_source_info, shader_path ) );
                    fs_file_info_t shader_output_info;
                    if ( fs->get_file_path_info ( &shader_output_info, binary_path ) ) {
                        if ( shader_output_info.last_write_time.count > shader_source_info.last_write_time.count
                            && shader_output_info.last_write_time.count > most_recent_header_edit.count     // if any header got updated after the shader was compiled, need to recompile
                        ) {
                            skip_compile = true;
                            ++shader_skip;
                        }
                    }

                    // invoke compiler process
                    if ( !skip_compile ) {
                        xs_shader_compiler_params_t params;
                        params.binary_path = binary_path;
                        params.shader_path = shader_path;
                        params.global_definitions = NULL;
                        params.shader_definition_count = 0; // TODO

                        bool compile_result = xs_shader_compiler_compile ( &params );

                        std_assert_m ( compile_result );

                        if ( !compile_result ) {
                            ++shader_fail;
                            continue;
                        } else {
                            ++shader_success;
                        }
                    }

                    // read generated shader bytecode
                    fs_file_h bytecode_file = fs->open_file ( binary_path, fs_file_read_m );
                    std_assert_m ( bytecode_file != fs_null_handle_m );

                    fs_file_info_t bytecode_file_info;
                    std_assert_m ( fs->get_file_info ( &bytecode_file_info, bytecode_file ) );

                    std_alloc_t alloc = std_virtual_heap_alloc ( bytecode_file_info.size, 16 );
                    shader_bytecode[stage].handle = alloc.handle;
                    std_assert_m ( fs->read_file ( NULL, alloc.buffer, bytecode_file ) );
                    shader_bytecode[stage].buffer = std_buffer ( alloc.buffer.base, bytecode_file_info.size );

                    fs->close_file ( bytecode_file );
                }
            }

            // TODO does this leak when there are multiple permutations?
            if ( shader_fail > 0 ) {
                ++result.failed_pipeline_states;
                result.failed_shaders += shader_fail;

                for ( size_t i = 0; i < xg_shading_stage_count_m; ++i ) {
                    std_virtual_heap_free ( shader_bytecode[i].handle );
                }

                continue;
            }

            pipeline_state->last_build_timestamp = std_timestamp_now_utc();

            xg_pipeline_state_h pipeline_handle = xg_null_handle_m;

            switch ( pipeline_state->type ) {
                case xg_pipeline_graphics_m:
                    if ( shader_references->referenced_stages & xg_shading_stage_bit_vertex_m ) {
                        graphics_state->params.state.vertex_shader.enable = true;
                        graphics_state->params.state.vertex_shader.buffer = shader_bytecode[xg_shading_stage_vertex_m].buffer;
                        graphics_state->params.state.vertex_shader.hash = std_hash_metro ( shader_bytecode[xg_shading_stage_vertex_m].buffer.base, shader_bytecode[xg_shading_stage_vertex_m].buffer.size );
                    }

                    if ( shader_references->referenced_stages & xg_shading_stage_bit_fragment_m ) {
                        graphics_state->params.state.fragment_shader.enable = true;
                        graphics_state->params.state.fragment_shader.buffer = shader_bytecode[xg_shading_stage_fragment_m].buffer;
                        graphics_state->params.state.fragment_shader.hash = std_hash_metro ( shader_bytecode[xg_shading_stage_fragment_m].buffer.base, shader_bytecode[xg_shading_stage_fragment_m].buffer.size );
                    }

                    for ( uint32_t i = 0; i < 20; ++i )
                        pipeline_handle = xg->create_graphics_pipeline ( device, &graphics_state->params );

                    break;

                case xg_pipeline_compute_m:
                    if ( shader_references->referenced_stages & xg_shading_stage_bit_compute_m ) {
                        compute_state->params.state.compute_shader.enable = true;
                        compute_state->params.state.compute_shader.buffer = shader_bytecode[xg_shading_stage_compute_m].buffer;
                        compute_state->params.state.compute_shader.hash = std_hash_metro ( shader_bytecode[xg_shading_stage_compute_m].buffer.base, shader_bytecode[xg_shading_stage_compute_m].buffer.size );
                    }

                    pipeline_handle = xg->create_compute_pipeline ( device, &compute_state->params );

                    break;
            }

            // TODO
            //xg_graphics_pipeline_state_h pipeline_handle = xg->create_graphics_pipeline ( device, &pipeline_params );
            std_assert_m ( pipeline_handle != xg_null_handle_m );

            // TODO add these pipelines to a separate list, to avoid having to iterate all pipelines in update_pipelines
            // Also, right now the case where it creates a new pipeline and there's already a new pipeline pending is broken!
            // The already pending pipeline is never deleted and leaked!
            pipeline_state->old_pipeline_handle = pipeline_state->pipeline_handle;
            pipeline_state->pipeline_handle = pipeline_handle;

            for ( size_t i = 0; i < xg_shading_stage_count_m; ++i ) {
                std_virtual_heap_free ( shader_bytecode[i].handle );
            }

            ++result.successful_pipeline_states;
            result.successful_shaders += shader_success;
            result.skipped_shaders += shader_skip;
        }
    }

    std_log_info_m ( "Shader database build: " std_fmt_size_m " states, " std_fmt_size_m " shaders built, " std_fmt_size_m " shaders skipped", 
        result.successful_pipeline_states, result.successful_shaders, result.skipped_shaders );

    if ( result.failed_shaders || result.failed_pipeline_states ) {
        std_log_warn_m ( "Shader database build: " std_fmt_size_m " states, " std_fmt_size_m " shaders failed" );
    }

    return result;
}

xs_pipeline_state_h xs_database_pipeline_lookup ( const char* name ) {
    xs_string_hash_t hash = xs_hash_string_m ( name, std_str_len ( name ) );
    xs_pipeline_state_h state = xs_database_pipeline_lookup_hash ( hash );
    return state;
}

xs_pipeline_state_h xs_database_pipeline_lookup_hash ( xs_string_hash_t hash ) {
    uint64_t* lookup = std_hash_map_lookup ( &xs_database_state->pipeline_name_hash_to_state_map, hash );

    if ( !lookup ) {
        std_log_error_m ( "Lookup for pipeline state failed" );
        return xg_null_handle_m;
    }

    xs_database_pipeline_state_t** state = ( xs_database_pipeline_state_t** ) lookup;
    xs_pipeline_state_h handle = ( xs_pipeline_state_h ) ( *state );
    return handle;
}

xg_graphics_pipeline_state_h xs_database_pipeline_get ( xs_pipeline_state_h handle ) {
    xs_database_pipeline_state_t* state = ( xs_database_pipeline_state_t* ) handle;
    return state->pipeline_handle;
}

void xs_database_update_pipelines ( xg_workload_h last_workload ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );

    for ( size_t state_it = 0; state_it < xs_database_state->pipeline_states_count; ++state_it ) {
        xs_database_pipeline_state_t* pipeline_state = &xs_database_state->pipeline_states[state_it];

        xg_graphics_pipeline_state_h old_pipeline_handle = pipeline_state->old_pipeline_handle;

        if ( old_pipeline_handle != xg_null_handle_m ) {
            xg_workload_h workload = pipeline_state->old_pipeline_workload;

            if ( workload == xg_null_handle_m ) {
                pipeline_state->old_pipeline_workload = last_workload;
                workload = last_workload;
            }

            if ( xg->is_workload_complete ( workload ) ) {
                if ( pipeline_state->type == xg_pipeline_graphics_m ) {
                    xg->destroy_graphics_pipeline ( pipeline_state->old_pipeline_handle );
                } else {
                    xg->destroy_compute_pipeline ( pipeline_state->old_pipeline_handle );
                }

                pipeline_state->old_pipeline_handle = xg_null_handle_m;
                pipeline_state->old_pipeline_workload = xg_null_handle_m;
            }
        }
    }
}
