#include <std_main.h>
#include <std_atomic.h>
#include <std_time.h>
#include <std_log.h>

#include <tk.h>

static tk_i* tk = NULL;
static uint32_t k = 0;

#define N 100

static void test_task_2 ( void* _arg ) {
    int* arg = ( int* ) _arg;

    for ( size_t i = 0; i < N; ++i ) {
        std_atomic_fetch_add_u32 ( &k, ( uint32_t ) *arg );
    }
}

static void test_task ( void* _arg ) {
    int* arg = ( int* ) _arg;

    tk_task_t task;
    task.routine =  test_task_2;
    int a = 1;
    task.arg = &a;

    tk_workload_h workloads[N];

    std_tick_t t1 = std_tick_now();

    for ( size_t i = 0; i < N; ++i ) {
        workloads[i] = tk->schedule_work ( &task, 1 );
    }

    for ( size_t i = 0; i < N; ++i ) {
        tk->wait_for_workload ( workloads[i] );
    }

    std_atomic_fetch_add_u32 ( &k, ( uint32_t ) *arg );

    std_tick_t t2 = std_tick_now();
    float dt = std_tick_to_milli_f32 ( t2 - t1 );

    std_log_info_m ( "Task chain complete. " std_fmt_f32_dec_m ( 2 ) "ms", dt );
}

static void test_tk() {
    tk = std_module_load_m ( tk_module_name_m );
    std_assert_m ( tk );

    size_t core_count = std_platform_logical_cores_info ( NULL, 0 );

    tk_thread_pool_params_t pool;
    pool.thread_count = ( uint32_t ) core_count - 1;
    pool.core_lock = true;
    tk->init_thread_pool ( &pool );

    std_thread_set_core_mask ( std_thread_this(), 1 << ( core_count - 1 ) );

    tk_task_t task;
    task.routine =  test_task;
    int arg = 1;
    task.arg = &arg;

    tk_workload_h workloads[N];

    for ( size_t i = 0; i < N; ++i ) {
        workloads[i] = tk->schedule_work ( &task, 1 );
    }

#if 1

    for ( size_t i = 0; i < N; ++i ) {
        // TODO do better than just spinning when waiting idle?
        tk->wait_for_workload_idle ( workloads[i] );
    }

#else
    tk_release_condition_b release_cause = tk->acquire_this_thread ( tk_release_condition_all_workloads_done_m, NULL );
    std_assert_m ( release_cause == tk_release_condition_all_workloads_done_m );
#endif

    std_assert_m ( k == N * N * N + N );
    std_log_info_m ( std_fmt_u32_m, k );

    tk->stop();

    std_module_unload_m ( tk_module_name_m );
}

void std_main ( void ) {
    test_tk();
    std_log_info_m ( "TK_TEST COMPLETE!" );
}
