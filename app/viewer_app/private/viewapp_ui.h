#pragma once

#include <wm.h>
#include <xg.h>

void viewapp_boot_ui ( xg_device_h device );
void viewapp_update_ui ( wm_window_info_t* window_info, wm_input_state_t* old_input_state, wm_input_state_t* input_state, xg_workload_h workload );
