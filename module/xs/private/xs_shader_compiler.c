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
        std_stack_t stack = std_static_stack_m ( args_buffer );

        args[argc++] = std_stack_string_copy ( &stack, "--target-env=vulkan1.2" );
        args[argc++] = std_stack_string_copy ( &stack, params->shader_path );
#if std_build_debug_m
        args[argc++] = std_stack_string_copy ( &stack, "-g" );
        args[argc++] = std_stack_string_copy ( &stack, "-O0" );
#else
        args[argc++] = std_stack_string_copy ( &stack, "-O" );
#endif
        args[argc++] = std_stack_string_copy ( &stack, "-o" );
        args[argc++] = std_stack_string_copy ( &stack, params->binary_path );

        {
            char include_path[256];
            std_string_t include_path_string = std_static_string_m ( include_path );
            std_string_append ( &include_path_string, "-I" );
            std_string_append ( &include_path_string, std_source_data_path_m );
            std_string_append ( &include_path_string, "/shader/" );
            args[argc++] = std_stack_string_copy ( &stack, include_path );
        }


        for ( size_t i = 0; i < params->global_definition_count; ++i ) {
            args[argc++] = std_stack_string_copy ( &stack, "-D" );
            std_stack_string_append ( &stack, params->global_definitions[i].name );
            std_stack_string_append ( &stack, "=" );
            char u32_buffer[32];
            size_t len = std_u32_to_str ( u32_buffer, 32, params->global_definitions[i].value, 0 );
            std_assert_m ( len > 0 && len < 32 );
            std_stack_string_append ( &stack, u32_buffer );
        }

        for ( size_t i = 0; i < params->shader_definition_count; ++i ) {
            args[argc++] = std_stack_string_copy ( &stack, "-D" );
            std_stack_string_append ( &stack, params->shader_definitions[i].name );
            std_stack_string_append ( &stack, "=" );
            char u32_buffer[32];
            size_t len = std_u32_to_str ( u32_buffer, 32, params->shader_definitions[i].value, 0 );
            std_assert_m ( len > 0 && len < 32 );
            std_stack_string_append ( &stack, u32_buffer );
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

        if ( read_result && read_size > 0 ) {
            std_log_warn_m ( stdout_output );
            result = false;
        }
    }

    char stderr_output[std_process_cmdline_max_len_m];
    {
        size_t read_size;
        bool read_result = std_process_io_read ( stderr_output, &read_size, std_process_cmdline_max_len_m, compiler_io.stderr_handle );
        std_assert_m ( read_size <= std_process_cmdline_max_len_m );

        if ( read_result && read_size > 0 ) {
            std_log_error_m ( stderr_output );
            result = false;
        }
    }

    return result;
}
