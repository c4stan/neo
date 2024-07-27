#pragma once

#include <xg.h>

#include "xg_cmd_buffer.h"
#include "xg_resource_cmd_buffer.h"
#include "xg_debug_capture.h"

#if xg_enable_backend_vulkan_m
    #include "vulkan/xg_vk_state.h"
#endif

#include <std_allocator.h>

typedef struct {
    xg_i api;
    xg_cmd_buffer_state_t cmd_buffer;
    xg_resource_cmd_buffer_state_t resource_cmd_buffer;
    xg_debug_capture_state_t debug_capture;
#if xg_enable_backend_vulkan_m
    xg_vk_state_t vk;
#endif
} xg_state_t;

std_module_declare_state_m ( xg )
