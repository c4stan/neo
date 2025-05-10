#pragma once

#include <std_allocator.h>

#include "xi_ui.h"
#include "xi_font.h"
#include "xi_workload.h"

#include <xs.h>

typedef struct {
    xi_i api;
    xs_database_h sdb;
    xi_ui_state_t ui;
    xi_font_state_t font;
    xi_workload_state_t workload;
} xi_state_t;

std_module_declare_state_m ( xi )

void xi_state_set_sdb ( xs_database_h sdb );
xs_database_h xi_state_get_sdb ( void );
