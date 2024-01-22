#pragma once

#include <std_buffer.h>
#include <xs.h>

typedef struct {
    const char* binary_path;
    const char* shader_path;
    const xs_shader_definition_t* global_definitions;
    uint32_t global_definition_count;
    const xs_shader_definition_t* shader_definitions;
    uint32_t shader_definition_count;
} xs_shader_compiler_params_t;

bool xs_shader_compiler_compile ( const xs_shader_compiler_params_t* params  );
