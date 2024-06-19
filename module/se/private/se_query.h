#pragma once

#include <se.h>

#include <std_mutex.h>

typedef struct {
    se_query_params_t params;
    uint32_t gen;
} se_pending_query_t;

typedef struct {
    //std_alloc_t result_alloc;
    se_query_result_t result;
    uint32_t gen;
} se_resolved_query_t;

typedef struct {
    se_pending_query_t* pending_queries;
    size_t pending_query_count;
    std_mutex_t pending_queries_mutex;

    se_resolved_query_t* resolved_queries;
    size_t resolved_query_count;
    std_mutex_t resolved_queries_mutex;
} se_query_state_t;

//void se_query_init ( void );

void se_query_load ( se_query_state_t* state );
void se_query_reload ( se_query_state_t* state );
void se_query_unload ( void );

se_query_h se_query_create ( const se_query_params_t* params );
void se_query_resolve ( void );
void se_query_results_dispose ( void );
const se_query_result_t* se_query_results_get ( se_query_h query );
