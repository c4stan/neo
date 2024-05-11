#pragma once

#include <xf.h>

#include "xf_graph.h"
#include "xf_resource.h"

#include <std_allocator.h>

typedef struct {
    xf_i api;
    xf_graph_state_t graph;
    xf_resource_state_t resource;
} xf_state_t;

std_module_declare_state_m ( xf )
