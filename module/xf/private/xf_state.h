#pragma once

#include <xf.h>

#include "xf_graph.h"
#include "xf_resource.h"

#include <std_allocator.h>

#include <xs.h>

typedef struct {
    xf_i api;
    xf_graph_state_t graph;
    xf_resource_state_t resource;
    xs_database_h sdb;
} xf_state_t;

std_module_declare_state_m ( xf )

void xf_state_set_sdb ( xs_database_h sdb );
xs_database_h xf_state_get_sdb ( void );
