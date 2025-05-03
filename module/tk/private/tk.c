#include <tk.h>

#include "tk_fiber.h"

typedef struct {
    tk_i api;
    tk_fiber_state_t fiber;
} tk_state_t;

std_module_declare_state_m ( tk );
std_module_implement_state_m ( tk );

static void tk_api_init ( tk_i* tk ) {
    tk->init_thread_pool = tk_fiber_thread_pool_init;
    tk->acquire_this_thread = tk_fiber_thread_this_acquire;
    tk->release_threads = tk_fiber_thread_release;
    tk->release_all_threads = tk_fiber_thread_release_all;
    tk->schedule_work = tk_fiber_workload_schedule;
    tk->wait_for_workload = tk_fiber_workload_wait;
    tk->wait_for_workload_idle = tk_fiber_workload_wait_idle;
    tk->stop = tk_fiber_scheduler_stop;
}

void* tk_load ( void* std_runtime ) {
    std_runtime_bind ( std_runtime );
    tk_state_t* state = tk_state_alloc();
    tk_fiber_load ( &state->fiber );
    tk_api_init ( &state->api );
    return &state->api;
}

void tk_reload ( void* std_runtime, void* api ) {
    std_runtime_bind ( std_runtime );
    tk_state_t* state = ( tk_state_t* ) api;
    tk_fiber_reload ( &state->fiber );
    tk_api_init ( &state->api );
    tk_state_bind ( state );
}

void tk_unload ( void ) {
    tk_fiber_unload();
    tk_state_free();
}
