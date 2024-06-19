#pragma once

#include "se_entity.h"
#include "se_query.h"

#include <std_module.h>

typedef struct {
    se_i api;
    se_entity_state_t entity;
    //se_query_state_t query;
} se_state_t;

std_module_declare_state_m ( se );
