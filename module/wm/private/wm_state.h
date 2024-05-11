#pragma once

#include <wm.h>

#include "wm_window.h"

typedef struct {
    wm_i api;
    wm_window_state_t window;
} wm_state_t;

std_module_declare_state_m ( wm )
