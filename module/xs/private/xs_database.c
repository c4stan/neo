#include "xs_database.h"

#include "xs_shader_compiler.h"
#include "xs_parser.h"

#include <xg_enum.h>

#include <std_allocator.h>
#include <std_list.h>
#include <std_string.h>
#include <std_log.h>
#include <std_file.h>

static xs_database_state_t* xs_database_state;

void xs_database_load ( xs_database_state_t* state ) {
    xs_database_state = state;

#if 0
    for ( size_t i = 0; i < xs_database_max_memory_pages_m; ++i ) {
        xs_database_memory_page_t* page = &xs_database_state->memory_pages[i];

        page->alloc = std_null_alloc_m;
        page->top = 0;
    }
#endif

    //xs_database_state->stack = std_virtual_stack_create ( xs_database_memory_pool_max_size_m );

    //xs_database_state->folders_count = 0;
    //xs_database_state->pipeline_states_count = 0;

    std_assert_m ( sizeof ( xs_string_hash_t ) <= sizeof ( uint64_t ) );
    std_assert_m ( sizeof ( xg_graphics_pipeline_state_h ) <= sizeof ( uint64_t ) );
    //static uint64_t pipeline_name_hash_array[xs_database_max_pipeline_states_m * 2];
    //static uint64_t pipeline_name_hash_payload_array[xs_database_max_pipeline_states_m * 2];

    //xs_database_state->pipeline_name_hash_to_state_map = std_hash_map ( pipeline_name_hash_array, pipeline_name_hash_payload_array, xs_database_max_pipeline_states_m * 2 );

    //xs_database_state->output_path[0] = '\0';
    //xs_database_state->pipeline_state_headers_last_build_timestamp = std_timestamp_zero_m;

    xs_database_state->database_array = std_virtual_heap_alloc_array_m ( xs_database_t, xs_database_max_databases_m );
    xs_database_state->database_freelist = std_freelist_m ( xs_database_state->database_array, xs_database_max_databases_m );
    std_mem_zero_m ( &xs_database_state->database_bitset );
}

void xs_database_reload ( xs_database_state_t* state ) {
    xs_database_state = state;
}

void xs_database_unload ( void ) {
    uint64_t db_idx = 0;
    while ( std_bitset_scan ( &db_idx, xs_database_state->database_bitset, db_idx, xs_database_bitset_u64_count_m ) ) {
        xs_database_h db_handle = db_idx;
        xs_database_destroy ( db_handle );
        ++db_idx;
    }

    std_virtual_heap_free ( xs_database_state->database_array );
}

static char* xs_database_alloc_string ( xs_database_t* db, size_t size ) {
#if 0
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
#endif
    void* alloc = std_virtual_stack_alloc ( &db->stack, size );
    return ( char* ) alloc;
}

typedef struct {
    xs_database_t* db;
    char* base;
} xs_database_folder_iterator_params_t;

static void xs_database_folder_iterator ( const char* name, std_path_flags_t flags, void* arg ) {
    if ( ! ( flags & std_path_is_file_m ) ) {
        return;
    }

    xs_database_folder_iterator_params_t* params = ( xs_database_folder_iterator_params_t* ) arg;

    size_t path_len = std_path_append ( params->base, std_path_size_m, name );
    bool is_header = false;
    xg_pipeline_e type;
    {
        size_t extension_base = std_str_find_reverse ( params->base, path_len, "." );

        if ( extension_base == std_str_find_null_m ) {
            std_path_pop ( params->base );
            return;
        }

        // TODO remove .xss
        if ( std_str_cmp ( params->base + extension_base, ".xsg" ) == 0 || std_str_cmp ( params->base + extension_base, ".xss" ) == 0 ) {
            type = xg_pipeline_graphics_m;
        } else if ( std_str_cmp ( params->base + extension_base, ".xsc" ) == 0 ) {
            type = xg_pipeline_compute_m;
#if xg_enable_raytracing_m
        } else if ( std_str_cmp ( params->base + extension_base, ".xsr" ) == 0 ) {
            type = xg_pipeline_raytrace_m;
#endif
        } else if ( std_str_cmp ( params->base + extension_base, ".glsl" ) == 0 ) {
            is_header = true;
        } else {
            std_path_pop ( params->base );
            return;
        }
    }

    char* dest = xs_database_alloc_string ( params->db, path_len + 1 );
    std_str_copy ( dest, path_len + 1, params->base );

    if ( is_header ) {
        xs_database_pipeline_state_header_t* header = &params->db->pipeline_state_headers_array[params->db->pipeline_state_headers_count++];
        header->path = dest;
    } else {
        xs_database_pipeline_state_t* pipeline_state = &params->db->pipeline_states[params->db->pipeline_states_count++];
        pipeline_state->path = dest;
        pipeline_state->name = std_path_name_ptr ( dest ); // TODO alloc separate string and remove .xss ext?
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
        bool unique = std_hash_map_insert ( &params->db->pipeline_name_hash_to_state_map, hash, ( uint64_t ) pipeline_state );
        std_assert_m ( unique );
    }

    std_path_pop ( params->base );
}

bool xs_database_add_folder ( xs_database_h db_handle, const char* input_path ) {
    xs_database_t* db = &xs_database_state->database_array[db_handle];

    char path[std_path_size_m] = { 0 };
    size_t path_len = std_path_absolute ( path, std_path_size_m, input_path );

    // TODO check that the path exists and is a folder

    char* dest = xs_database_alloc_string ( db, path_len + 1 );
    std_str_copy ( dest, path_len + 1, path );
    db->folders[db->folders_count++] = dest;

    xs_database_folder_iterator_params_t args;
    args.db = db;
    args.base = path;
    std_directory_iterate ( path, xs_database_folder_iterator, &args );

    return true;
}

bool xs_database_set_output_folder ( xs_database_h db_handle, const char* input_path ) {
    xs_database_t* db = &xs_database_state->database_array[db_handle];

    std_path_info_t info;
    bool result = std_path_info ( &info, input_path );
    result &= info.flags & std_path_is_directory_m;

    if ( !result ) {
        result = std_directory_create ( input_path );
    }

    char path[std_path_size_m] = { 0 };
    std_path_absolute ( path, std_path_size_m, input_path );

    std_str_copy ( db->output_path, std_path_size_m, path );

    return result;
}

void xs_database_clear ( xs_database_h db_handle ) {
#if 0
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
#endif
    // TODO is this ever used? missing some stuff?
    xs_database_t* db = &xs_database_state->database_array[db_handle];
    
    std_virtual_stack_clear ( &db->stack );

    db->folders_count = 0;
    db->pipeline_states_count = 0;
}

void xs_database_set_build_params ( xs_database_h db_handle, const xs_database_build_params_t* params ) {
    xs_database_t* db = &xs_database_state->database_array[db_handle];

    if ( params->base_graphics_state ) {
        db->base_graphics_state = *params->base_graphics_state;
    }

    if ( params->base_compute_state ) {
        db->base_compute_state = *params->base_compute_state;
    }

    if ( params->base_raytrace_state ) {
        db->base_raytrace_state = *params->base_raytrace_state;
    }

    db->global_definition_count = params->global_definition_count;
    for ( uint32_t i = 0; i < params->global_definition_count; ++i ) {
        db->global_definitions[i] = params->global_definitions[i];
    }

    db->dirty_build_params = true;
}

void xs_database_rebuild_all ( void ) {
    uint64_t db_idx = 0;
    while ( std_bitset_scan ( &db_idx, xs_database_state->database_bitset, db_idx, xs_database_bitset_u64_count_m ) ) {
        xs_database_h db_handle = db_idx;
        xs_database_build ( db_handle );
        ++db_idx;
    }
}

typedef struct {
    xg_shading_stage_e stage;
    std_buffer_t buffer;
} shader_bytecode_t; 

static void xs_database_set_pipeline_state_shader ( xg_pipeline_state_shader_t* shader, const shader_bytecode_t* bytecode ) {
    shader->enable = true;
    shader->stage = bytecode->stage;
    shader->hash = std_hash_block_64_m ( bytecode->buffer.base, bytecode->buffer.size );
    shader->buffer = bytecode->buffer;
}

xs_database_build_result_t xs_database_build ( xs_database_h db_handle ) {
    xs_database_t* db = &xs_database_state->database_array[db_handle];

    xs_database_build_result_t result = xs_database_build_result_m();

    xg_i* xg = std_module_get_m ( xg_module_name_m );

    const bool verbose = false;
    if ( verbose ) {
        std_log_info_m ( "Starting build for database " std_fmt_str_m, db->debug_name );
    }

    // check headers, froce rebuild all shaders if one changed
    // TODO try to take dependencies into account and avoid rebuilding everything?
    // TODO account for xs.glsl changes
    bool dirty_headers = false;
    std_timestamp_t most_recent_header_edit = std_timestamp_zero_m;

    for ( size_t header_it = 0; header_it < db->pipeline_state_headers_count && !dirty_headers; ++header_it ) {
        xs_database_pipeline_state_header_t* header = &db->pipeline_state_headers_array[header_it];

        std_file_info_t info;
        std_verify_m ( std_file_path_info ( &info, header->path ) );

        bool dirty = db->pipeline_state_headers_last_build_timestamp.count < info.last_write_time.count;
        dirty_headers |= dirty;

        if ( verbose && dirty ) {
            std_log_info_m ( "Header " std_fmt_str_m " found dirty: " std_fmt_u64_m " " std_fmt_u64_m, header->path, db->pipeline_state_headers_last_build_timestamp.count, info.last_write_time.count );
        }
    
        if ( info.last_write_time.count > most_recent_header_edit.count ) {
            most_recent_header_edit = info.last_write_time;
        }
    }

    if ( dirty_headers ) {
        db->pipeline_state_headers_last_build_timestamp = std_timestamp_now_utc();
    }

    // check pipeline states
    for ( size_t state_it = 0; state_it < db->pipeline_states_count; ++state_it ) {
        xs_database_pipeline_state_t* pipeline_state = &db->pipeline_states[state_it];

        if ( verbose ) {
            std_log_info_m ( "Parsing pipeline metadata " std_fmt_str_m, pipeline_state->path );
        }

        std_file_h pipeline_state_file = std_file_open ( pipeline_state->path, std_file_read_m );
        std_assert_m ( pipeline_state_file != std_file_null_handle_m );

        std_file_info_t pipeline_state_file_info;
        std_verify_m ( std_file_info ( &pipeline_state_file_info, pipeline_state_file ) );

        std_file_close ( pipeline_state_file );

        bool state_parse_result = false;

        typedef union {
            xs_parser_graphics_pipeline_state_t graphics;
            xs_parser_compute_pipeline_state_t compute;
            xs_parser_raytrace_pipeline_state_t raytrace;
        } xs_parsed_pipeline_state_t;

        xs_parsed_pipeline_state_t parsed_pipeline_state = {};

        xs_parser_shader_references_t* shader_references;
        xs_parser_shader_definitions_t* shader_definitions;
        xg_resource_bindings_layout_params_t* resource_layouts;

        // TODO move the parsing down to after checking for last build timestamp etc?
        switch ( pipeline_state->type ) {
            case xg_pipeline_graphics_m:
                parsed_pipeline_state.graphics.params = xg_graphics_pipeline_params_m (
                    .state = db->base_graphics_state
                );
                std_str_copy_static_m ( parsed_pipeline_state.graphics.params.debug_name, pipeline_state->name );

                state_parse_result = xs_parser_parse_graphics_pipeline_state_from_path ( &parsed_pipeline_state.graphics, pipeline_state->path );

                shader_references = &parsed_pipeline_state.graphics.shader_references;
                shader_definitions = &parsed_pipeline_state.graphics.shader_definitions;
                resource_layouts = parsed_pipeline_state.graphics.resource_layouts;
                break;

            case xg_pipeline_compute_m:
                parsed_pipeline_state.compute.params = xg_compute_pipeline_params_m (
                    .state = db->base_compute_state
                );
                std_str_copy_static_m ( parsed_pipeline_state.compute.params.debug_name, pipeline_state->name );

                state_parse_result = xs_parser_parse_compute_pipeline_state_from_path ( &parsed_pipeline_state.compute, pipeline_state->path );

                shader_references = &parsed_pipeline_state.compute.shader_references;
                shader_definitions = &parsed_pipeline_state.compute.shader_definitions;
                resource_layouts = parsed_pipeline_state.compute.resource_layouts;
                break;

            case xg_pipeline_raytrace_m:
                parsed_pipeline_state.raytrace.params = xg_raytrace_pipeline_params_m (
                    .state = db->base_raytrace_state
                );
                std_str_copy_static_m ( parsed_pipeline_state.raytrace.params.debug_name, pipeline_state->name );

                state_parse_result = xs_parser_parse_raytrace_pipeline_state_from_path ( &parsed_pipeline_state.raytrace, pipeline_state->path );

                shader_references = &parsed_pipeline_state.raytrace.shader_references;
                shader_definitions = &parsed_pipeline_state.raytrace.shader_definitions;
                resource_layouts = parsed_pipeline_state.raytrace.resource_layouts;
                break;
        }

        if ( !state_parse_result ) {
            if ( verbose ) {
                std_log_info_m ( "Pipeline " std_fmt_str_m " failed to parse", pipeline_state->path );
            }
            ++result.failed_pipeline_states;
            continue;
        }

        char input_path[std_path_size_m];
        std_str_copy ( input_path, std_path_size_m, pipeline_state->path );
        std_path_pop ( input_path );

        char output_path[std_path_size_m];

        if ( db->output_path[0] ) {
            std_str_copy ( output_path, std_path_size_m, db->output_path );
        } else {
            std_str_copy ( output_path, std_path_size_m, input_path );
        }

        std_directory_create ( output_path );

        shader_bytecode_t shader_bytecode[xs_shader_parser_max_shader_references_m];
        char shader_path[std_path_size_m];
        char binary_path[std_path_size_m];

        // check if the pipeline state needs to be (re)built
        bool needs_to_build = dirty_headers || pipeline_state->last_build_timestamp.count < pipeline_state_file_info.last_write_time.count || db->dirty_build_params;

        if ( !needs_to_build ) {
            #if 0
            for ( xg_shading_stage_e stage = 0; stage < xg_shading_stage_count_m; ++stage ) {
                if ( shader_references->referenced_stages & xg_shading_stage_enum_to_bit_m ( stage ) ) {
                    std_str_copy ( shader_path, fs_path_size_m, input_path );
                    fs->append_path ( shader_path, fs_path_size_m, shader_references->shaders[stage] );

                    // check shader source edit time
                    fs_file_info_t shader_source_info;
                    std_verify_m ( fs->get_file_path_info ( &shader_source_info, shader_path ) );

                    if ( pipeline_state->last_build_timestamp.count < shader_source_info.last_write_time.count ) {
                        needs_to_build = true;
                        break;
                    }
                }
            }
            #else
            for ( uint32_t i = 0; i < shader_references->count; ++i ) {
                xs_parser_shader_reference_t* shader = &shader_references->array[i];
                std_str_copy ( shader_path, std_path_size_m, input_path );
                std_path_append ( shader_path, std_path_size_m, shader->name );

                // check shader source edit time
                std_file_info_t shader_source_info;
                std_verify_m ( std_file_path_info ( &shader_source_info, shader_path ) );

                if ( pipeline_state->last_build_timestamp.count < shader_source_info.last_write_time.count ) {
                    needs_to_build = true;
                    break;
                }
            }
            #endif
        }

        if ( !needs_to_build ) {
            if ( verbose ) {
                std_log_info_m ( "Skipping pipeline " std_fmt_str_m, pipeline_state->path );
            }
            result.skipped_shaders += shader_references->count;
            result.skipped_pipeline_states += 1;
            continue;
        }

        {
            xs_parser_graphics_pipeline_state_t* graphics_state = &parsed_pipeline_state.graphics;
            xs_parser_compute_pipeline_state_t* compute_state = &parsed_pipeline_state.compute;
            xs_parser_raytrace_pipeline_state_t* raytrace_state = &parsed_pipeline_state.raytrace;

            size_t shader_success = 0;
            size_t shader_fail = 0;
            size_t shader_skip = 0;

            //for ( xg_shading_stage_e stage = 0; stage < xg_shading_stage_count_m; ++stage ) {
            //    if ( shader_references->referenced_stages & xg_shading_stage_enum_to_bit_m ( stage ) ) {
            for ( uint32_t i = 0; i < shader_references->count; ++i ) {
                xs_parser_shader_reference_t* shader = &shader_references->array[i];

                shader_bytecode_t* bytecode = &shader_bytecode[i];

                // prepare shader code input path
                std_str_copy ( shader_path, std_path_size_m, input_path );
                std_path_append ( shader_path, std_path_size_m, shader->name );

                // prepare shader bytecode output path
                std_str_copy ( binary_path, std_path_size_m, output_path );
                std_path_append ( binary_path, std_path_size_m, shader->name );
                size_t len = std_str_len ( binary_path );
                size_t len2 = std_str_find_reverse ( binary_path, len, "." );

                if ( len2 != std_str_find_null_m ) {
                    len = len2;
                }

                std_stack_t stack = std_static_stack_m ( binary_path );
                stack.top = stack.begin + len + 1;

                const char* stage_tag = "";

                xg_shading_stage_e stage = shader->stage;
                if ( stage == xg_shading_stage_vertex_m ) {
                    stage_tag = "vs";
                } else if ( stage == xg_shading_stage_fragment_m ) {
                    stage_tag = "fs";
                } else if ( stage == xg_shading_stage_compute_m ) {
                    stage_tag = "cs";
                } else if ( stage == xg_shading_stage_ray_gen_m ) {
                    stage_tag = "rg";
                } else if ( stage == xg_shading_stage_ray_miss_m ) {
                    stage_tag = "rm";
                } else if ( stage == xg_shading_stage_ray_hit_closest_m ) {
                    stage_tag = "rhc";
                }

                //std_str_append ( binary_path, &array, "-" );
                //std_str_append ( binary_path, &array, stage_tag );
                //std_str_append ( binary_path, &array, ".spv" );

                std_stack_string_append ( &stack, "-" );
                std_stack_string_append ( &stack, stage_tag );
                std_stack_string_append ( &stack, ".spv" );

                // check if shader needs to be (re)compiled
                bool skip_compile = false;
                std_file_info_t shader_source_info;
                std_verify_m ( std_file_path_info ( &shader_source_info, shader_path ) );
                std_file_info_t shader_output_info;
                if ( std_file_path_info ( &shader_output_info, binary_path ) ) {
                    if ( shader_output_info.last_write_time.count > shader_source_info.last_write_time.count
                        && shader_output_info.last_write_time.count > most_recent_header_edit.count     // if any header got updated after the shader was compiled, need to recompile
                    ) {
                        skip_compile = true;
                        ++shader_skip;
                    }
                }

                if ( verbose ) {
                    if ( skip_compile ) {
                        std_log_info_m ( "Skipping shader " std_fmt_str_m, shader_path );
                    } else {
                        std_log_info_m ( "Building shader " std_fmt_str_m " to " std_fmt_str_m, shader_path, binary_path );
                    }
                }

                // invoke compiler process
                if ( !skip_compile ) {
                    xs_shader_compiler_params_t params;
                    params.binary_path = binary_path;
                    params.shader_path = shader_path;
                    params.global_definitions = db->global_definitions;
                    params.global_definition_count = db->global_definition_count;
                    params.shader_definitions = shader_definitions->array;
                    params.shader_definition_count = shader_definitions->count;

                    bool compile_result = xs_shader_compiler_compile ( &params );

                    if ( !compile_result ) {
                        if ( verbose ) {
                            std_log_info_m ( "Shader " std_fmt_str_m " failed to build", shader_path );
                        }
                        ++shader_fail;
                        bytecode->buffer = std_buffer_m();
                        continue;
                    } else {
                        ++shader_success;
                    }
                }

                // read generated shader bytecode
                #if 0
                fs_file_h bytecode_file = fs->open_file ( binary_path, fs_file_read_m );
                std_assert_m ( bytecode_file != fs_null_handle_m );

                fs_file_info_t bytecode_file_info;
                std_verify_m ( fs->get_file_info ( &bytecode_file_info, bytecode_file ) );

                {
                    size_t size = bytecode_file_info.size;
                    void* alloc = std_virtual_heap_alloc ( size, 16 );
                    shader_bytecode[stage].size = bytecode_file_info.size;
                    std_verify_m ( fs->read_file ( alloc, size, bytecode_file ) );
                    shader_bytecode[stage].base = alloc;
                    shader_bytecode[stage].size = size;
                }
                fs->close_file ( bytecode_file );
                #else
                if ( verbose ) {
                    std_log_info_m ( "Loading shader binary " std_fmt_str_m " from disk", shader_path, binary_path );
                }
                bytecode->stage = stage;
                bytecode->buffer = std_file_read_to_virtual_heap ( binary_path );
                #endif
                //}
            }

            // TODO does this leak when there are multiple permutations?
            if ( shader_fail > 0 ) {
                ++result.failed_pipeline_states;
                result.failed_shaders += shader_fail;

                for ( size_t i = 0; i < shader_references->count; ++i ) {
                    std_virtual_heap_free ( shader_bytecode[i].buffer.base );
                }

                continue;
            }

            pipeline_state->last_build_timestamp = std_timestamp_now_utc();

            xg_pipeline_state_h pipeline_handle = xg_null_handle_m;

            for ( uint32_t i = 0; i < shader_references->count; ++i ) {
                shader_bytecode_t* shader = &shader_bytecode[i];
                std_assert_m ( shader->buffer.size > 0 );

                switch ( shader->stage ) {
                case xg_shading_stage_vertex_m:
                    std_assert_m ( pipeline_state->type == xg_pipeline_graphics_m );
                    xs_database_set_pipeline_state_shader ( &graphics_state->params.state.vertex_shader, shader );
                    break;
                case xg_shading_stage_fragment_m:
                    std_assert_m ( pipeline_state->type == xg_pipeline_graphics_m );
                    xs_database_set_pipeline_state_shader ( &graphics_state->params.state.fragment_shader, shader );
                    break;
                case xg_shading_stage_compute_m:
                    std_assert_m ( pipeline_state->type == xg_pipeline_compute_m );
                    xs_database_set_pipeline_state_shader ( &compute_state->params.state.compute_shader, shader );
                    break;
                case xg_shading_stage_ray_gen_m:
                case xg_shading_stage_ray_hit_closest_m:
                case xg_shading_stage_ray_miss_m:
                    std_assert_m ( pipeline_state->type == xg_pipeline_raytrace_m );
                    xg_pipeline_state_shader_t* pipeline_shader = &raytrace_state->params.state.shader_state.shaders[raytrace_state->params.state.shader_state.shader_count++];
                    xs_database_set_pipeline_state_shader ( pipeline_shader, shader );
                    break;
                default:
                    std_assert_m ( false );
                    break;
                }
            }

            for ( uint32_t i = 0; i < xg_shader_binding_set_count_m; ++i ) {
                xg_resource_bindings_layout_params_t* layout_params = &resource_layouts[i];
                std_stack_t stack = std_static_stack_m ( layout_params->debug_name );
                std_stack_string_append ( &stack, pipeline_state->name );
                std_stack_string_append ( &stack, "-" );
                std_stack_string_append ( &stack, xg_shader_binding_set_str ( i ) );
                xg_resource_bindings_layout_h resource_layout = xg->create_resource_layout ( layout_params );
                pipeline_state->resource_layouts[i] = resource_layout;

                switch ( pipeline_state->type ) {
                case xg_pipeline_graphics_m:
                    graphics_state->params.resource_layouts[i] = resource_layout;
                    break;
                case xg_pipeline_compute_m:
                    compute_state->params.resource_layouts[i] = resource_layout;
                    break;
                case xg_pipeline_raytrace_m:
                    raytrace_state->params.resource_layouts[i] = resource_layout;
                    break;
                }
            }

            if ( verbose ) {
                std_log_info_m ( "Creating pipeline state " std_fmt_str_m, pipeline_state->name );
            }
            switch ( pipeline_state->type ) {
                case xg_pipeline_graphics_m:
                    pipeline_handle = xg->create_graphics_pipeline ( db->device, &graphics_state->params );
                    break;
                case xg_pipeline_compute_m:
                    pipeline_handle = xg->create_compute_pipeline ( db->device, &compute_state->params );
                    break;
                case xg_pipeline_raytrace_m:
                    pipeline_handle = xg->create_raytrace_pipeline ( db->device, &raytrace_state->params );
                    break;
            }

            if ( pipeline_handle == xg_null_handle_m ) {
                std_log_warn_m ( "Pipeline state " std_fmt_str_m " creation failed", pipeline_state->name );
            }

            // TODO add these pipelines to a separate list, to avoid having to iterate all pipelines in update_pipelines
            // Also, right now the case where it creates a new pipeline and there's already a new pipeline pending is broken!
            // The already pending pipeline is never deleted and leaked!
            pipeline_state->old_pipeline_handle = pipeline_state->pipeline_handle;
            pipeline_state->pipeline_handle = pipeline_handle;

            for ( size_t i = 0; i < shader_references->count; ++i ) {
                std_virtual_heap_free ( shader_bytecode[i].buffer.base );
            }

            ++result.successful_pipeline_states;
            result.successful_shaders += shader_success;
            result.skipped_shaders += shader_skip;
        }
    }

    std_log_info_m ( "Shader database build " std_fmt_str_m std_fmt_newline_m 
        "Pipeline states: " std_fmt_tab_m std_fmt_u32_pad_m(3) " failed " std_fmt_tab_m std_fmt_u32_pad_m(3) " built " std_fmt_tab_m std_fmt_u32_pad_m(3) " cached" std_fmt_newline_m 
        "Shaders: " std_fmt_tab_m std_fmt_tab_m std_fmt_u32_pad_m(3) " failed " std_fmt_tab_m std_fmt_u32_pad_m(3) " built " std_fmt_tab_m std_fmt_u32_pad_m(3) " cached", db->debug_name,
        result.failed_pipeline_states, result.successful_pipeline_states, result.skipped_pipeline_states,
        result.failed_shaders, result.successful_shaders, result.skipped_shaders );

    if ( result.failed_shaders || result.failed_pipeline_states ) {
        std_log_warn_m ( "Shader database build: " std_fmt_size_m " states, " std_fmt_size_m " shaders failed", result.failed_pipeline_states, result.failed_shaders );
    }

    return result;
}

xs_database_pipeline_h xs_database_pipeline_get ( xs_database_h db_handle, xs_string_hash_t hash ) {
    xs_database_t* db = &xs_database_state->database_array[db_handle];
    uint64_t* lookup = std_hash_map_lookup ( &db->pipeline_name_hash_to_state_map, hash );

    if ( !lookup ) {
        std_log_error_m ( "Lookup for pipeline state failed" );
        return xs_null_handle_m;
    }

    xs_database_pipeline_state_t** state = ( xs_database_pipeline_state_t** ) lookup;
    xs_database_pipeline_h handle = ( xs_database_pipeline_h ) ( *state );
    return handle;
}

xg_graphics_pipeline_state_h xs_database_pipeline_state_get ( xs_database_pipeline_h handle ) {
    if ( handle == xs_null_handle_m ) {
        return xg_null_handle_m;
    }

    xs_database_pipeline_state_t* state = ( xs_database_pipeline_state_t* ) handle;
    return state->pipeline_handle;
}

void xs_database_update_pipelines ( xg_workload_h last_workload ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );

    uint64_t db_idx = 0;
    while ( std_bitset_scan ( &db_idx, xs_database_state->database_bitset, db_idx, xs_database_bitset_u64_count_m ) ) {
        xs_database_t* db = &xs_database_state->database_array[db_idx];

        for ( size_t state_it = 0; state_it < db->pipeline_states_count; ++state_it ) {
            xs_database_pipeline_state_t* pipeline_state = &db->pipeline_states[state_it];

            xg_graphics_pipeline_state_h old_pipeline_handle = pipeline_state->old_pipeline_handle;

            if ( old_pipeline_handle != xg_null_handle_m ) {
                xg_workload_h workload = pipeline_state->old_pipeline_workload;

                if ( workload == xg_null_handle_m ) {
                    pipeline_state->old_pipeline_workload = last_workload;
                    workload = last_workload;
                }

                if ( xg->is_workload_complete ( workload ) ) {
                    switch ( pipeline_state->type ) {
                    case xg_pipeline_graphics_m:
                        xg->destroy_graphics_pipeline ( pipeline_state->old_pipeline_handle );
                        break;
                    case xg_pipeline_compute_m:
                        xg->destroy_compute_pipeline ( pipeline_state->old_pipeline_handle );
                        break;
                    case xg_pipeline_raytrace_m:
                        xg->destroy_raytrace_pipeline ( pipeline_state->old_pipeline_handle );
                        break;
                    }

                    pipeline_state->old_pipeline_handle = xg_null_handle_m;
                    pipeline_state->old_pipeline_workload = xg_null_handle_m;
                }
            }
        }

        ++db_idx;
    }
}

xs_database_h xs_database_create ( const xs_database_params_t* params ) {
    xs_database_t* db = std_list_pop_m ( &xs_database_state->database_freelist );

    std_mem_zero_m ( db );
    db->device = params->device;
    db->stack = std_virtual_stack_create ( xs_database_memory_pool_max_size_m );
    db->pipeline_name_hash_to_state_map = std_hash_map_create ( xs_database_max_pipeline_states_m * 2 );
    db->pipeline_state_headers_array = std_virtual_heap_alloc_array_m ( xs_database_pipeline_state_header_t, xs_database_max_pipeline_state_headers_m );
    std_str_copy_static_m ( db->debug_name, params->debug_name );

    db->base_graphics_state = xg_graphics_pipeline_state_m();
    db->base_compute_state = xg_compute_pipeline_state_m();
    db->base_raytrace_state = xg_raytrace_pipeline_state_m();
    std_mem_zero_static_array_m ( db->global_definitions );
    db->global_definition_count = 0;
    db->dirty_build_params = false;

    uint64_t db_idx = db - xs_database_state->database_array;
    std_bitset_set ( xs_database_state->database_bitset, db_idx );

    xs_database_h db_handle = db_idx;

    return db_handle;
}

void xs_database_destroy ( xs_database_h db_handle ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );
    
    xs_database_t* db = &xs_database_state->database_array[db_handle];

    for ( uint32_t i = 0; i < db->pipeline_states_count; ++i ) {
        xs_database_pipeline_state_t* state = &db->pipeline_states[i];

        if ( state->pipeline_handle != xg_null_handle_m ) {
            switch ( state->type ) {
            case xg_pipeline_graphics_m:
                xg->destroy_graphics_pipeline ( state->pipeline_handle );
                break;
            case xg_pipeline_compute_m:
                xg->destroy_compute_pipeline ( state->pipeline_handle );
                break;
            case xg_pipeline_raytrace_m:
                xg->destroy_raytrace_pipeline ( state->pipeline_handle );
                break;
            }
        }

        for ( uint32_t i = 0; i < xg_shader_binding_set_count_m; ++i ) {
            xg->destroy_resource_layout ( state->resource_layouts[i] );
        }
    }

    std_virtual_heap_free ( db->pipeline_state_headers_array );
    std_hash_map_destroy ( &db->pipeline_name_hash_to_state_map );
    std_virtual_stack_destroy ( &db->stack );

    std_bitset_clear ( xs_database_state->database_bitset, db_handle );
}
