#pragma once

#include <xs.h>

#include "xs_database.h"

typedef struct {
    xs_i api;
    //std_memory_h memory_handle;
    xs_database_state_t database;
} xs_state_t;

std_module_declare_state_m ( xs )
