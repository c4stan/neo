#include <std_main.h>
#include <std_atomic.h>
#include <std_time.h>
#include <std_log.h>

#include <tk.h>

static tk_i* tk = NULL;

#define PARALLEL 0

#define N_DATA 500000000
#define TASKS_T1 100
#define TASKS_T2 100
#define COUNT_T1 ( N_DATA / TASKS_T1 )
#define COUNT_T2 ( N_DATA / ( TASKS_T1 * TASKS_T2 ) )

std_warnings_ignore_m ( "-Wunused-variable" );

typedef struct {
    uint64_t* base;
    uint64_t count;
    uint64_t result;
    void* t2;
} task_args_t;

static void test_task_2 ( void* _arg ) {
    std_auto_m arg = ( task_args_t* ) _arg;

    uint64_t sum = 0;

    for ( size_t i = 0; i < arg->count; ++i ) {
        sum += arg->base[i];
    }

    arg->result = sum;
}

static void test_task_1 ( void* _arg ) {
    std_auto_m arg = ( task_args_t* ) _arg;

    tk_task_t tasks[TASKS_T2];

    std_tick_t t1 = std_tick_now();

    for ( size_t i = 0; i < TASKS_T2; ++i ) {
        tk_task_t* task = tasks + i;
        task->routine =  test_task_2;
        task->arg = ( task_args_t* ) arg->t2 + i;

        task_args_t* arg_t2 = ( task_args_t* ) arg->t2 + i;
        arg_t2->base = arg->base + i * COUNT_T2;
        arg_t2->count = COUNT_T2;
        arg_t2->result = 0;
        arg_t2->t2 = NULL;
        
    #if !PARALLEL
        test_task_2 ( arg_t2 );
    #endif
    }

#if PARALLEL
    tk_workload_h workload = tk->schedule_work ( tasks, TASKS_T2 );
    tk->wait_for_workload ( workload );
#endif

    uint64_t sum = 0;

    for ( size_t i = 0; i < TASKS_T2; ++i ) {
        task_args_t* arg_t2 = ( task_args_t* ) arg->t2 + i;
        sum += arg_t2->result;
    }

    std_assert_m ( sum == TASKS_T2 * COUNT_T2 );

    arg->result = sum;

    std_tick_t t2 = std_tick_now();
    float dt = std_tick_to_milli_f32 ( t2 - t1 );

    std_log_info_m ( "Task chain complete. " std_fmt_f32_dec_m ( 2 ) "ms", dt );
}

static void test_tk() {
    tk = std_module_load_m ( tk_module_name_m );
    std_assert_m ( tk );

    size_t core_count = std_platform_logical_cores_info ( NULL, 0 );

#if PARALLEL
    tk_thread_pool_params_t pool;
    pool.thread_count = ( uint32_t ) core_count - 1;
    pool.core_lock = true;
    tk->init_thread_pool ( &pool );

    std_thread_set_core_mask ( std_thread_this(), 1 << ( core_count - 1 ) );
#endif

    uint64_t* data = std_virtual_heap_alloc_array_m ( uint64_t, N_DATA );
    task_args_t* args = std_virtual_heap_alloc_array_m ( task_args_t, TASKS_T1 + TASKS_T1 * TASKS_T2 );

    for ( uint64_t i = 0; i < N_DATA; ++i ) {
        data[i] = 1;
    }

    for ( uint64_t i = 0; i < TASKS_T1; ++i ) {
        args[i].base = data + i * COUNT_T1;
        args[i].count = COUNT_T1;
        args[i].result = 0;
        args[i].t2 = args + TASKS_T1 + i * TASKS_T2;
    }

    tk_task_t tasks[TASKS_T1];

    std_tick_t t1 = std_tick_now();

    for ( size_t i = 0; i < TASKS_T1; ++i ) {
        tk_task_t* task = tasks + i;
        task->routine =  test_task_1;
        task->arg = args + i;
    #if !PARALLEL
        test_task_1 ( args + i );
    #endif
    }

#if 1

#if PARALLEL
    tk_workload_h workload = tk->schedule_work ( tasks, TASKS_T1 );
    // TODO do better than just spinning when waiting idle?
    tk->wait_for_workload_idle ( workload );
#endif

#else
    tk_release_condition_b release_cause = tk->acquire_this_thread ( tk_release_condition_all_workloads_done_m, NULL );
    std_assert_m ( release_cause == tk_release_condition_all_workloads_done_m );
#endif

    uint64_t sum = 0;

    for ( size_t i = 0; i < TASKS_T1; ++i ) {
        task_args_t* arg_t1 = args + i;
        sum += arg_t1->result;
    }

    std_tick_t t2 = std_tick_now();
    float dt = std_tick_to_milli_f32 ( t2 - t1 );

    std_assert_m ( sum == N_DATA );
    std_log_info_m ( std_fmt_u32_m " in " std_fmt_f32_dec_m ( 2 ) "ms", sum, dt );

    tk->stop();

    std_module_unload_m ( tk_module_name_m );
}

void std_main ( void ) {
    test_tk();
    std_log_info_m ( "TK_TEST COMPLETE!" );
}
