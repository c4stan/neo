#include "viewapp_state.h"

std_module_implement_state_m ( viewapp );

viewapp_state_t* viewapp_state_get ( void ) {
    return viewapp_state;
}
