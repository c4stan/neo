#include "xf_state.h"

std_module_implement_state_m ( xf )

void xf_state_set_sdb ( xs_database_h sdb ) {
    xf_state->sdb = sdb;
}

xs_database_h xf_state_get_sdb ( void ) {
    return xf_state->sdb;
}
