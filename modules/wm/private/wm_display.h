#pragma once

#include <wm.h>

size_t wm_display_get_count ( void );
size_t wm_display_get ( wm_display_h* displays, size_t cap );

bool wm_display_get_info ( wm_display_info_t* info, wm_display_h display );

size_t wm_display_get_modes_count ( wm_display_h display );
bool wm_display_get_modes ( wm_display_mode_t* modes, size_t cap, wm_display_h display );
