#pragma once

#include <std_allocator.h>

#include "xi_element.h"
#include "xi_font.h"
#include "xi_workload.h"

typedef struct {
    xi_i api;
    xi_element_state_t element;
    xi_font_state_t font;
    xi_workload_state_t workload;
} xi_state_t;

std_module_declare_state_m ( xi )
