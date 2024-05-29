#pragma once

#include <std_allocator.h>

#include "xi_ui.h"
#include "xi_font.h"
#include "xi_workload.h"

typedef struct {
    xi_i api;
    xi_ui_state_t ui;
    xi_font_state_t font;
    xi_workload_state_t workload;
} xi_state_t;

std_module_declare_state_m ( xi )
