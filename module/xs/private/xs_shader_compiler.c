#include "xs_shader_compiler.h"

#include <std_process.h>
#include <std_log.h>
#include <std_string.h>

bool xs_shader_compiler_compile ( const xs_shader_compiler_params_t* params ) {
    char executable[std_process_path_max_len_m];
    std_str_copy ( executable, std_process_path_max_len_m, std_pp_eval_string_m ( std_binding_shader_compiler_m ) );

#if 0
    const char* entry_point = "main";
    const char* stage_name = "";
    const char* stage_tag = "";

    if ( stage == xg_shading_stage_vertex_m ) {
        entry_point = "vs_main";
        stage_name = "vertex";
        stage_tag = "vs";
    } else if ( stage == xg_shading_stage_fragment_m ) {
        entry_point = "fs_main";
        stage_name = "fragment";
        stage_tag = "fs";
    } else if ( stage == xg_shading_stage_compute_m ) {
        entry_point = "cs_main";
        stage_name = "compute";
        stage_tag = "cs";
    }

#endif

    const char* args[std_process_max_args_m];
    size_t argc = 0;
    char args_buffer[std_process_args_max_len_m] = {0};
    //char cmdline[std_process_cmdline_max_len_m];
    {
        std_array_t array = std_static_array_m ( args_buffer );

        args[argc++] = &args_buffer[array.count];
        std_str_append ( args_buffer, &array, params->shader_path );

        std_array_push ( &array, 1 );
        args[argc++] = &args_buffer[array.count];
        std_str_append ( args_buffer, &array, "-g" );

        std_array_push ( &array, 1 );
        args[argc++] = &args_buffer[array.count];
#if std_enabled_m(std_debug_m)
        std_str_append ( args_buffer, &array, "-O0" );
#else
        std_str_append ( args_buffer, &array, "-O" );
#endif

        std_array_push ( &array, 1 );
        args[argc++] = &args_buffer[array.count];
        std_str_append ( args_buffer, &array, "-o" );

        /*char binary_name[xs_shader_name_max_len_m];
        {
            char* dest2 = binary_name;
            size_t cap2 = xs_shader_name_max_len_m;
            std_str_append_m ( dest2, cap2, output_path );
            //std_str_copy ( binary_name, xs_shader_name_max_len_m, shader_path );

            size_t len = std_str_len ( binary_name );
            len = std_str_find_reverse ( binary_name, len, "." );
            dest2 = binary_name + len;
            cap2 = xs_shader_name_max_len_m - len;
            std_str_append_m ( dest2, cap2, ".spv" );
        }*/
        std_array_push ( &array, 1 );
        args[argc++] = &args_buffer[array.count];
        std_str_append ( args_buffer, &array, params->binary_path );

#if 0
        args[argc++] = ++dest;
        std_str_append_m ( dest, cap, "-fshader-stage=" );
        std_str_append_m ( dest, cap, stage_name );

        args[argc++] = ++dest;
        std_str_append_m ( dest, cap, "-D" );
        std_str_append_m ( dest, cap, entry_point );
        std_str_append_m ( dest, cap, "=main" );
#endif

        std_array_push ( &array, 1 );
        args[argc++] = &args_buffer[array.count];
        // TODO find a better way to do this... this depends on the working dir (the app workspace root when running from neo)
        {
            char include_path[256];
            std_array_t include_path_array = std_static_array_m ( include_path );
            std_str_append ( include_path, &include_path_array, "-I" );
            std_str_append ( include_path, &include_path_array, std_module_path_m );
            std_str_append ( include_path, &include_path_array, "public/shader/" );
            std_str_append ( args_buffer, &array, include_path );
        }

        //if ( binary_name_out ) {
        //    std_str_copy ( binary_name_out, out_cap, binary_name );
        //}

        if ( params->global_definitions != NULL && params->global_definition_count > 0 ) {
            char u32_buffer[32];

            for ( size_t i = 0; i < params->global_definition_count; ++i ) {
                std_array_push ( &array, 1 );
                args[argc++] = &args_buffer[array.count];
                std_str_append ( args_buffer, &array, "-D" );
                std_str_append ( args_buffer, &array, params->global_definitions[i].name );
                std_str_append ( args_buffer, &array, "=" );
                size_t len = std_u32_to_str ( params->global_definitions[i].value, u32_buffer, 32 );
                std_assert_m ( len > 0 && len < 32 );
                std_str_append ( args_buffer, &array, u32_buffer );
            }

            for ( size_t i = 0; i < params->shader_definition_count; ++i ) {
                std_array_push ( &array, 1 );
                args[argc++] = &args_buffer[array.count];
                std_str_append ( args_buffer, &array, "-D" );
                std_str_append ( args_buffer, &array, params->global_definitions[i].name );
                std_str_append ( args_buffer, &array, "=" );
                size_t len = std_u32_to_str ( params->global_definitions[i].value, u32_buffer, 32 );
                std_assert_m ( len > 0 && len < 32 );
                std_str_append ( args_buffer, &array, u32_buffer );
            }
        }
    }

    bool result = true;
    std_process_h compiler = std_process ( executable, "xs-glslc", args, argc, std_process_type_default_m, std_process_io_capture_m );
    std_process_io_t compiler_io = std_process_get_io ( compiler );
    {
        bool wait_result = std_process_wait_for ( compiler );
        std_assert_m ( wait_result );
        result &= wait_result;
    }

    // Return failure if any warnings or errors were output
    char stdout_output[std_process_cmdline_max_len_m];
    {
        size_t read_size;
        bool read_result = std_process_io_read ( stdout_output, &read_size, std_process_cmdline_max_len_m, compiler_io.stdout_handle );
        std_assert_m ( read_size <= std_process_cmdline_max_len_m );

        if ( read_result ) {
            std_log_warn_m ( stdout_output );
        }

        result &= !read_result;
    }

    char stderr_output[std_process_cmdline_max_len_m];
    {
        size_t read_size;
        bool read_result = std_process_io_read ( stderr_output, &read_size, std_process_cmdline_max_len_m, compiler_io.stderr_handle );
        std_assert_m ( read_size <= std_process_cmdline_max_len_m );

        if ( read_result ) {
            std_log_error_m ( stderr_output );
        }

        result &= !read_result;
    }

    return result;
}
