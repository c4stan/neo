#include "xi_update.h"

#include "xi_ui.h"

void xi_update_begin ( const xi_update_params_t* params ) {
    xi_ui_update_begin ( params->window_info, params->input_state, params->input_buffer, params->view_info );
}

void xi_update_end ( void ) {
    xi_ui_update_end();
}
