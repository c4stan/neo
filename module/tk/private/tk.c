#include <tk.h>

#include "tk_fiber.h"

static tk_i s_api;

static tk_i tk_api ( void ) {
    tk_i tk;
    tk.init_thread_pool = tk_fiber_thread_pool_init;
    tk.acquire_this_thread = tk_fiber_thread_this_acquire;
    tk.release_threads = tk_fiber_thread_release;
    tk.release_all_threads = tk_fiber_thread_release_all;
    tk.schedule_work = tk_fiber_workload_schedule;
    tk.wait_for_workload = tk_fiber_workload_wait;
    tk.wait_for_workload_idle = tk_fiber_workload_wait_idle;
    tk.stop = tk_fiber_scheduler_stop;
    return tk;
}

void* tk_load ( void* std_runtime ) {
    std_runtime_bind ( std_runtime );
    tk_fiber_init();
    s_api = tk_api();
    return &s_api;
}

void tk_unload ( void ) {
    std_noop_m;
}
