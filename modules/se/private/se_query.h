#pragma once

#include <se.h>

void se_query_init ( void );

se_query_h se_query_create ( const se_query_params_t* params );
void se_query_resolve ( void );
void se_query_results_dispose ( void );
const se_query_result_t* se_query_results_get ( se_query_h query );
