#pragma once

#include <std_module.h>
#include <std_thread.h>

#define tk_module_name_m tk
std_module_export_m void* tk_load ( void* );
std_module_export_m void tk_unload ( void );

typedef uint64_t tk_workload_h;

typedef void ( tk_task_routine_f ) ( void* );

typedef struct {
    tk_task_routine_f* routine;
    void* arg;
} tk_task_t;

// TODO support a number of args instead of single arg?
//#define tk_task_def_m( name, arg_t ) void name ( void* arg )

/*
    - Init fixed will create a thread pool of the given size and core affinity internal to the scheduler.
      Any work submitted will be run on these threads.
    - Init optimal will depend on the current backend implementation.
      For a fiber backend, it will most likely spawn one thread per core, minus one left for the main thread.
      When (if) the main thread calls acquire hinting at the fact that it is, in fact, the main thread, its
      thread affinity will be changed to match the core left out for it. Alternatively,
      TODO what about a thread backend?
    - After a suspend call, the user can call resume. If that happens, the scheduler will wake back up its
      thread pool and get back to work. Eventual external threads will have to call acquire again, if they
      also want to be part of the thread pool.
*/

typedef enum {
    tk_release_condition_user_request_m = 1 << 0,
    // Checked every time a task is completed. If the worker notices that there's no other active or pending
    // task, all acquired workers listening on this condition are signaled and will return once their current
    // task is complete.
    tk_release_condition_all_workloads_done_m = 1 << 1,
} tk_release_condition_b;

typedef uint64_t tk_acquired_thread_h;

typedef struct {
    uint32_t thread_count;
    bool core_lock;
} tk_thread_pool_params_t;

typedef struct {
    void                    ( *init_thread_pool )               ( const tk_thread_pool_params_t* params );

    tk_release_condition_b  ( *acquire_this_thread )            ( tk_release_condition_b release_condition, tk_acquired_thread_h* out_handle );
    void                    ( *release_threads )                ( const tk_acquired_thread_h* threads, size_t count );
    void                    ( *release_all_threads )            ( void );

    tk_workload_h           ( *schedule_work )                  ( const tk_task_t* tasks, size_t count );
    void                    ( *wait_for_workload )              ( const tk_workload_h workload );
    void                    ( *wait_for_workload_idle )         ( const tk_workload_h workload );
    //void                    ( *wait_for_workloads )             ( const tk_workload_h* workloads, size_t count );

    //void                    ( *start )                          ( void );
    //void                    ( *pause )                          ( void );
    //void                    ( *resume )                         ( void );
    void                    ( *stop )                           ( void );

    //size_t                  ( acquired_threads_count )          ( void );
    //size_t                  ( owned_threads_count )             ( void );
} tk_i;
