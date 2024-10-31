#pragma once

#include <xi.h>

// TODO: update params should not be globally stored as part of xi_ui/geo state, but as part of the workload

void xi_update_begin ( const xi_update_params_t* params );
void xi_update_end ( void );
