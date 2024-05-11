#pragma once

#include <fs.h>

#include <std_allocator.h>

typedef struct {
    fs_i api;
} fs_state_t;

std_module_declare_state_m ( fs )
