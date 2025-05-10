#include "xi_state.h"

#include <std_module.h>

std_module_implement_state_m ( xi )

void xi_state_set_sdb ( xs_database_h sdb ) {
    xi_state->sdb = sdb;
}

xs_database_h xi_state_get_sdb ( void ) {
    return xi_state->sdb;
}
