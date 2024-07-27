#pragma once

#include <xg.h>

#include "xg_vk.h"
#include "xg_vk_device.h"
#include "xg_cmd_buffer.h"

void xg_vk_cmd_buffer_load ( xg_vk_cmd_buffer_state_t* state );
void xg_vk_cmd_buffer_reload ( xg_vk_cmd_buffer_state_t* state );
void xg_vk_cmd_buffer_unload ( void );

void xg_vk_cmd_buffer_activate_device ( xg_device_h device_handle );
void xg_vk_cmd_buffer_deactivate_device ( xg_device_h device_handle );

void xg_vk_cmd_buffer_submit ( xg_workload_h workload );
