#pragma once

#include <std_allocator.h>

#include "xui_element.h"
#include "xui_font.h"
#include "xui_workload.h"

typedef struct {
    xui_i api;
    std_memory_h memory_handle;
    xui_element_state_t element;
    xui_font_state_t font;
    xui_workload_state_t workload;
} xui_state_t;

std_module_declare_state_m ( xui )
