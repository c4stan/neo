#pragma once

typedef enum {
    std_app_state_tick_m,
    std_app_state_reload_m, // Reload modules code, keep state
    std_app_state_reboot_m, // Unload and load modules from new
    std_app_state_exit_m,
} std_app_state_e;

typedef struct {
    std_app_state_e ( *tick ) ( void );
} std_app_i;
