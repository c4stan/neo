#pragma once

#include <rv.h>

#include <std_allocator.h>

#include "rv_view.h"

typedef struct {
    rv_i api;
    std_memory_h memory_handle;
    rv_view_state_t view;
    //rv_visible_state_t visible;
    //rv_query_state_t query;
} rv_state_t;

std_module_declare_state_m ( rv )
