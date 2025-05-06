#include "tk_fiber.h"

#include <std_platform.h>
#include <std_thread.h>
#include <std_atomic.h>
#include <std_list.h>
#include <std_mutex.h>

#if defined(std_platform_linux_m)
    #include <ucontext.h>
#endif

// ------------------------------------------------------------------------------------------------

static void tk_fiber_thread_entry_point ( void* arg );
static void tk_fiber_entry_point ( void* arg );

static void tk_fiber_create ( tk_fiber_context_t* context, void ( routine ) ( void* ) );
static void tk_fiber_destroy ( tk_fiber_context_t* context );
static void tk_fiber_switch ( tk_fiber_context_t* old_fiber, tk_fiber_context_t* new_fiber );
static void tk_fiber_enter_from_thread ( tk_fiber_context_t* main_context );
static void tk_fiber_exit_to_thread ( tk_fiber_context_t* main_context );

static tk_fiber_thread_context_t* tk_fiber_thread_context ( void );

static tk_fiber_workload_h tk_fiber_schedule_tasks ( const tk_task_t* tasks, uint32_t tasks_count );

static void release_acquired_thread ( tk_acquired_thread_t* thread, tk_release_condition_b cause );
static bool check_for_all_workloads_done ( void );
static void acquired_threads_on_all_workloads_done ( void );

// ------------------------------------------------------------------------------------------------

static tk_fiber_state_t* tk_fiber_state;

void tk_fiber_load ( tk_fiber_state_t* state ) {
    tk_fiber_state = state;

    state->threads_array = std_virtual_heap_alloc_array_m ( std_thread_h, tk_max_threads_m );
    state->thread_contexts_array = std_virtual_heap_alloc_array_m ( tk_fiber_thread_context_t, tk_max_threads_m );
    state->fiber_contexts_array = std_virtual_heap_alloc_array_m ( tk_fiber_context_t, tk_max_fibers_m );
    state->workload_array = std_virtual_heap_alloc_array_m ( tk_fiber_workload_t, tk_max_parallel_tasks_m );
    state->acquired_threads_array = std_virtual_heap_alloc_array_m ( tk_acquired_thread_t, tk_max_threads_m );

    std_mem_zero_array_m ( state->threads_array, tk_max_threads_m );
    std_mem_zero_array_m ( state->thread_contexts_array, tk_max_threads_m );
    std_mem_zero_array_m ( state->fiber_contexts_array, tk_max_fibers_m );
    std_mem_zero_array_m ( state->workload_array, tk_max_parallel_tasks_m );
    std_mem_zero_array_m ( state->acquired_threads_array, tk_max_threads_m );

    state->acquired_threads_freelist = std_freelist_m ( state->acquired_threads_array, tk_max_threads_m );
    std_mutex_init ( &state->acquired_threads_mutex );

    state->dispatch_queue = std_queue_shared_create ( tk_max_parallel_tasks_m * sizeof ( tk_fiber_dispatched_task_t ) );
    state->ready_fiber_contexts = std_queue_mpmc_32_create ( tk_max_fibers_m * sizeof ( uint32_t ) );
    state->free_fiber_contexts = std_queue_mpmc_32_create ( tk_max_fibers_m * sizeof ( uint32_t ) );
    state->free_workloads = std_queue_mpmc_32_create ( tk_max_parallel_tasks_m * sizeof ( uint32_t ) );

    for ( size_t i = 0; i < tk_max_parallel_tasks_m; ++i ) {
        uint32_t idx = ( uint32_t ) i;
        state->workload_array[i].wait_list.head = tk_fiber_idx_null_m;
        std_queue_mpmc_push_32 ( &state->free_workloads, &idx );
    }

    for ( size_t i = 0; i < tk_max_fibers_m; ++i ) {
        tk_fiber_create ( &state->fiber_contexts_array[i], tk_fiber_entry_point );
        uint32_t idx = ( uint32_t ) i;
        state->fiber_contexts_array[i].id = idx;
        state->fiber_contexts_array[i].wait_list_next = tk_fiber_idx_null_m;
        std_queue_mpmc_push_32 ( &state->free_fiber_contexts, &idx );
    }

    for ( size_t i = 0; i < tk_max_threads_m; ++i ) {
        state->acquired_threads_array[i].thread_handle = std_thread_null_handle_m;
        state->threads_array[i] = std_thread_null_handle_m;
    }
}

void tk_fiber_reload ( tk_fiber_state_t* state ) {
    tk_fiber_state = state;
}

void tk_fiber_unload ( void ) {
    for ( uint32_t i = 0; i < tk_max_fibers_m; ++i ) {
        tk_fiber_destroy ( &tk_fiber_state->fiber_contexts_array[i] );
    }

    std_virtual_heap_free ( tk_fiber_state->threads_array );
    std_virtual_heap_free ( tk_fiber_state->thread_contexts_array );
    std_virtual_heap_free ( tk_fiber_state->fiber_contexts_array );
    std_virtual_heap_free ( tk_fiber_state->workload_array );
    std_virtual_heap_free ( tk_fiber_state->acquired_threads_array );

    std_queue_shared_destroy ( &tk_fiber_state->dispatch_queue );
    std_queue_shared_destroy ( &tk_fiber_state->ready_fiber_contexts );
    std_queue_shared_destroy ( &tk_fiber_state->free_fiber_contexts );
    std_queue_shared_destroy ( &tk_fiber_state->free_workloads );

    std_mutex_deinit ( &tk_fiber_state->acquired_threads_mutex );
}

// ------------------------------------------------------------------------------------------------
// Entry points

static void tk_fiber_thread_entry_point ( void* arg ) {
    std_unused_m ( arg );
    tk_fiber_thread_context_t* context = tk_fiber_thread_context();
    tk_fiber_enter_from_thread ( &context->main_fiber );
    context->current_fiber = tk_fiber_pop_context();
    tk_fiber_switch ( &context->main_fiber, context->current_fiber );
    // exit current context --->
    // ..
    // ..
    // <--- return
    tk_fiber_switch ( context->current_fiber, &context->main_fiber );
    tk_fiber_pool_context ( context->current_fiber );
    tk_fiber_exit_to_thread ( &context->main_fiber );
}

static void tk_fiber_entry_point ( void* arg ) {
    std_unused_m ( arg );
    tk_fiber_on_context_return();
    tk_fiber_thread_context_t* thread_context = tk_fiber_thread_context();
    tk_fiber_dispatched_task_t dispatch;

    // while running
    while ( !thread_context->join_flag && !tk_fiber_state->join_flag ) {

        // TODO which one is better? should prioritize picking up new tasks or finishing up old ones?
#if 1
        // try running a new task
        if ( std_queue_mpmc_pop_m ( &tk_fiber_state->dispatch_queue, &dispatch ) ) {
            dispatch.task.routine ( dispatch.task.arg );
            tk_fiber_on_task_completed ( &dispatch );
        } else {
            uint32_t ready_context_idx;

            // try running a task that has become ready
            if  ( std_queue_mpmc_pop_move_32 ( &tk_fiber_state->ready_fiber_contexts, &ready_context_idx ) ) {
                tk_fiber_yield_to ( &tk_fiber_state->fiber_contexts_array[ready_context_idx] );
            } else {
                // yield if there's nothing to run
                std_thread_this_yield();
            }
        }

#else
        uint32_t ready_context_idx;

        // try running a task that has become ready
        if  ( std_queue_mpmc_pop_move_32 ( &tk_fiber_state->ready_fiber_contexts, &ready_context_idx ) ) {
            tk_fiber_yield_to ( &tk_fiber_state->fiber_contexts_array[ready_context_idx] );
            // try running a new task
        } else if ( std_queue_mpmc_pop_m ( &tk_fiber_state->dispatch_queue, &dispatch ) ) {
            dispatch.task.routine ( dispatch.task.arg );
            tk_fiber_on_task_completed ( &dispatch );
        } else {
            // yield if there's nothing to run
            std_thread_this_yield();
        }

#endif
        // Refresh thread context pointer
        thread_context = tk_fiber_thread_context();
    }

    tk_fiber_switch ( thread_context->current_fiber, &thread_context->main_fiber );
}

// ------------------------------------------------------------------------------------------------
// Fiber system API interface

static void tk_fiber_switch ( tk_fiber_context_t* old_fiber, tk_fiber_context_t* new_fiber ) {
#if defined(std_platform_win32_m)
    std_unused_m ( old_fiber );
    SwitchToFiber ( ( void* ) new_fiber->os_handle );
#elif defined(std_platform_linux_m)
    swapcontext ( &old_fiber->os_context, &new_fiber->os_context );
#endif
}

static void tk_fiber_create ( tk_fiber_context_t* context, void ( routine ) ( void* ) ) {
#if defined(std_platform_win32_m)
    context->os_handle = ( tk_fiber_h ) ( CreateFiberEx ( 0, 0, FIBER_FLAG_FLOAT_SWITCH, routine, NULL ) );
#elif defined(std_platform_linux_m)
    context->stack = std_virtual_heap_alloc_m ( std_thread_stack_size_m, 16 );
    getcontext ( &context->os_context );
    context->os_context.uc_stack.ss_sp = context->stack;
    context->os_context.uc_stack.ss_size = std_thread_stack_size_m;
    context->os_context.uc_link = 0;
    makecontext ( &context->os_context, ( void ( * ) () ) routine, 1, NULL );
#endif
}

static void tk_fiber_destroy ( tk_fiber_context_t* context ) {
#if defined(std_platform_win32_m)
    DeleteFiber ( context->os_handle );
#elif defined(std_platform_linux_m)
    std_virtual_heap_free ( context->stack );
#endif
}

static void tk_fiber_enter_from_thread ( tk_fiber_context_t* context ) {
    context->id = 0;
    context->wait_list_next = 0;
#if defined(std_platform_win32_m)
    context->os_handle = ( tk_fiber_h ) ( ConvertThreadToFiberEx ( NULL, FIBER_FLAG_FLOAT_SWITCH ) );
#elif defined(std_platform_linux_m)
    context->stack = std_virtual_heap_alloc_m ( std_thread_stack_size_m, 16 );
    getcontext ( &context->os_context );
    context->os_context.uc_stack.ss_size = 0;
    context->os_context.uc_stack.ss_sp = 0;
    context->os_context.uc_link = 0;
#endif
}

static void tk_fiber_exit_to_thread ( tk_fiber_context_t* context ) {
#if defined(std_platform_win32_m)
    std_unused_m ( context );
    ConvertFiberToThread();
#elif defined(std_platform_linux_m)
    std_log_info_m ( std_fmt_ptr_m, context->stack );
    std_virtual_heap_free ( context->stack );
#endif
}

// ------------------------------------------------------------------------------------------------
// Private code

void tk_fiber_wake_up ( tk_fiber_context_t* context ) {
    uint32_t idx = ( uint32_t ) ( context - tk_fiber_state->fiber_contexts_array );

    for ( ;; ) {
        bool retval = std_queue_mpmc_push_32 ( &tk_fiber_state->ready_fiber_contexts, &idx );
        if ( retval ) {
            break;
        }
    }
}

void tk_fiber_on_task_completed ( const tk_fiber_dispatched_task_t* dispatch ) {
    // Decrement and fast out if counter is not yet 0
    uint32_t counter = std_atomic_decrement_u32 ( &dispatch->workload->remaining_tasks_count );

    if ( counter != 0 ) {
        return;
    }

    // Close the wait list
    tk_fiber_workload_t* workload = dispatch->workload;
    // TODO: does this need to be a CAS to properly synchronize with the CAS in tk_fiber_wait_list_insert?
    //  the assumption here is that after the ++ and the mem fence no CAS from wait_list_insert can possibly succeed
    workload->wait_list.gen++;

    std_memory_fence(); // Want the gen increase to happen before the read to the wait list head

    // Process the wait list
    // Foreach item in it we need to transition it to waiting to be executed
    uint32_t prev = workload->wait_list.head;
    uint32_t curr = prev;

    while ( curr != tk_fiber_idx_null_m ) {
        tk_fiber_context_t* ctx = &tk_fiber_state->fiber_contexts_array[curr];
        tk_fiber_wake_up ( ctx );
        prev = curr;
        curr = ctx->wait_list_next;
        tk_fiber_state->fiber_contexts_array[prev].wait_list_next = tk_fiber_idx_null_m;
    }

    // Clean up and pool back the workload
    workload->wait_list.head = tk_fiber_idx_null_m;
    uint32_t workload_idx = ( uint32_t ) ( workload - tk_fiber_state->workload_array );

    for ( ;; ) {
        bool retval = std_queue_mpmc_push_32 ( &tk_fiber_state->free_workloads, &workload_idx );

        if ( retval ) {
            break;
        }
    }

    // If all workloads are completed release acquired threads that are waiting for that
    if ( check_for_all_workloads_done() ) {
        acquired_threads_on_all_workloads_done();
    }
}

// Add given fiber to the wait list of the fiber it's waiting on
bool tk_fiber_wait_list_insert ( tk_fiber_paused_context_t* fiber ) {
    tk_fiber_workload_h pausing_workload_handle = fiber->pausing_workload;
    tk_fiber_workload_t* pausing_workload = &tk_fiber_state->workload_array[pausing_workload_handle.idx];

    // The write never actually changes between tries
    tk_fiber_wait_list_t write;
    write.gen = pausing_workload_handle.gen;
    write.head = ( uint32_t ) ( fiber->self - tk_fiber_state->fiber_contexts_array ); // fiber context idx

    // Insert the fiber context into the wait list
    tk_fiber_wait_list_t read = pausing_workload->wait_list;

    do {
        // Validate the read
        if ( read.gen != pausing_workload_handle.gen ) {
            // The job we're trying to wait on is already done.
            tk_fiber_wake_up ( fiber->self );
            return false;
        }

        // Prepare the context and try to insert
        fiber->self->wait_list_next = read.head;
    } while ( !std_compare_and_swap_u64 ( &pausing_workload->wait_list.u64, &read.u64, write.u64 ) );

    return true;
}

void tk_fiber_pool_context ( tk_fiber_context_t* fiber ) {
    uint32_t i = ( uint32_t ) ( fiber - tk_fiber_state->fiber_contexts_array );
    fiber->wait_list_next = tk_fiber_idx_null_m;

    for ( ;; ) {
        // TODO refactor the loop?
        bool retval = std_queue_mpmc_push_32 ( &tk_fiber_state->free_fiber_contexts, &i );

        if ( retval ) {
            break;
        }
    }
}

static tk_fiber_thread_context_t* tk_fiber_thread_context ( void ) {
    return &tk_fiber_state->thread_contexts_array[std_thread_index ( std_thread_this() )];
}

// This needs to get called whenever a fiber resumes or begins its execution.
// In practice that's either after a tk_fiber_switch call or at the very beginning of the fiber main routime
// Notice that when calling tk_fiber_switch the current fiber will be paused in favor of some other, but eventually
// when it will get resumed, execution will resume from there.
void tk_fiber_on_context_return ( void ) {
    // We just got into this fiber from somewhere else. Time to check thread state tracking.
    tk_fiber_thread_context_t* context = tk_fiber_thread_context();

    if ( context->prev_fiber.self != NULL ) {
        if ( context->prev_fiber.pausing_workload.idx != tk_fiber_idx_null_m ) {
            // If we are executing because some other fiber went to sleep, we must add it to the
            // appropriate wait list.
            tk_fiber_wait_list_insert ( &context->prev_fiber );
            context->prev_fiber.self = NULL;
        } else {
            // If the previous fiber wasn't waiting on anything, it means that it's done. Pool it back.
            tk_fiber_pool_context ( context->prev_fiber.self );
        }
    }
}

void tk_fiber_yield_to ( tk_fiber_context_t* new_fiber ) {
    // Setup context
    tk_fiber_thread_context_t* context = tk_fiber_thread_context();
    context->prev_fiber.self = context->current_fiber;
    context->prev_fiber.pausing_workload.idx = tk_fiber_idx_null_m;
    context->current_fiber = new_fiber;
    // Switch
    tk_fiber_switch ( context->prev_fiber.self, new_fiber );
    // exit current context --->
    // ..
    // ..
    // <--- return/enter any prev. paused or new context
    tk_fiber_on_context_return();
}

void tk_fiber_wait_for ( tk_fiber_workload_h workload ) {
    // Setup context
    tk_fiber_thread_context_t* context = tk_fiber_thread_context();
    context->prev_fiber.self = context->current_fiber;
    context->prev_fiber.pausing_workload.u64 = workload.u64;
    tk_fiber_context_t* new_fiber = tk_fiber_pop_context();
    context->current_fiber = new_fiber;
    // Switch
    tk_fiber_switch ( context->prev_fiber.self, new_fiber );
    // exit current context --->
    // ..
    // ..
    // <--- return/enter any prev. paused or new context
    tk_fiber_on_context_return();
}

tk_fiber_context_t* tk_fiber_pop_context ( void ) {
    tk_fiber_context_t* fiber;

    for ( ;; ) {
        uint32_t i = 0;

        // Try to run fibers that have woken up first
        if ( std_queue_mpmc_pop_move_32 ( &tk_fiber_state->ready_fiber_contexts, &i ) ) {
            fiber = &tk_fiber_state->fiber_contexts_array[i];
            break;
        }

        // Try to run a new dispatched fiber next
        if ( std_queue_mpmc_pop_move_32 ( &tk_fiber_state->free_fiber_contexts, &i ) ) {
            fiber = &tk_fiber_state->fiber_contexts_array[i];
            break;
        }

        // TODO check pool sizes
        //std_thread_this_yield();
    }

    return fiber;
}

// ------------------------------------------------------------------------------------------------
// Public API

static tk_fiber_workload_h tk_fiber_schedule_tasks ( const tk_task_t* tasks, uint32_t tasks_count ) {
    uint32_t i;

    while ( !std_queue_mpmc_pop_move_32 ( &tk_fiber_state->free_workloads, &i )  ) {
        //std_thread_this_yield();
    }

    tk_fiber_workload_h workload_handle;
    tk_fiber_workload_t* workload = &tk_fiber_state->workload_array[i];
    workload_handle.idx = i;
    workload_handle.gen = workload->wait_list.gen;
    workload->remaining_tasks_count = tasks_count;

    for ( size_t j = 0; j < tasks_count; ++j ) {
        tk_fiber_dispatched_task_t dispatch;
        dispatch.workload = workload;
        dispatch.task = tasks[j];

        while ( !std_queue_mpmc_push_m ( &tk_fiber_state->dispatch_queue, &dispatch ) ) {
            //std_thread_this_yield();
        }
    }

    return workload_handle;
}

tk_workload_h tk_fiber_workload_schedule ( const tk_task_t* tasks, size_t tasks_count ) {
    tk_fiber_workload_h workload = tk_fiber_schedule_tasks ( tasks, ( uint32_t ) tasks_count );
    return ( tk_workload_h ) workload.u64;
}

void tk_fiber_workload_wait ( tk_workload_h handle ) {
    // Cast to tk_fiber_workload_h and get workload ptr
    tk_fiber_workload_h workload_handle;
    workload_handle.u64 = handle;
    const tk_fiber_workload_t* workload = &tk_fiber_state->workload_array[workload_handle.idx];

    // Test for bad handle or already executed workload
    if ( workload->wait_list.gen != workload_handle.gen || workload->remaining_tasks_count == 0 ) {
        return;
    }

    tk_fiber_wait_for ( workload_handle );
}

void tk_fiber_workload_wait_idle ( tk_workload_h handle ) {
    // Cast to tk_fiber_workload_h and get workload ptr
    tk_fiber_workload_h workload_handle;
    workload_handle.u64 = handle;
    const tk_fiber_workload_t* workload = &tk_fiber_state->workload_array[workload_handle.idx];

    // Wait for workload completion
    while ( workload->wait_list.gen == workload_handle.gen && workload->remaining_tasks_count != 0 ) {
        std_thread_this_yield();
    }
}

void tk_fiber_thread_pool_init ( const tk_thread_pool_params_t* params ) {
    size_t system_core_count = std_platform_logical_cores_info ( NULL, 0 );

    uint32_t thread_count = params->thread_count;

    if ( thread_count > tk_max_threads_m ) {
        std_log_warn_m ( "Max thread count allowed is lower than provided thread count." );
        thread_count = tk_max_threads_m;
    }

    if ( thread_count > system_core_count ) {
        std_log_warn_m ( "tk fiber thread pool is being initialized with more workers than logical cores" );
    }

    std_assert_m ( thread_count < 64 );

    for ( size_t i = 0; i < thread_count; ++i ) {
        char name[32];
        std_str_format ( name, 32, "TK" std_fmt_size_m, i );
        uint64_t core_mask = params->core_lock ? 1 << i : std_thread_core_mask_any_m;
        tk_fiber_state->threads_array[i] = std_thread ( tk_fiber_thread_entry_point, NULL, name, core_mask );
    }

    tk_fiber_state->thread_count = thread_count;
}

tk_release_condition_b tk_fiber_thread_this_acquire ( tk_release_condition_b release_condition, tk_acquired_thread_h* out_handle ) {
    std_mutex_lock ( &tk_fiber_state->acquired_threads_mutex );

    // If all workloads are already done early out, avoid potentially locking in the thread forever.
    // An alternatice might be checking for all_workloads_done inside thread_entry_point rather than
    // on_task_completed.
    if ( release_condition & tk_release_condition_all_workloads_done_m ) {
        if ( check_for_all_workloads_done() ) {
            std_mutex_unlock ( &tk_fiber_state->acquired_threads_mutex );
            return tk_release_condition_all_workloads_done_m;
        }
    }

    tk_acquired_thread_t* acquired_thread = std_list_pop_m ( &tk_fiber_state->acquired_threads_freelist );
    std_assert_m ( acquired_thread != NULL );
    acquired_thread->thread_handle = std_thread_this();
    acquired_thread->release_condition = release_condition;
    tk_fiber_thread_context_t* thread_context = tk_fiber_thread_context();
    thread_context->join_flag = false;

    if ( out_handle ) {
        *out_handle = ( tk_acquired_thread_h ) ( acquired_thread - tk_fiber_state->acquired_threads_array );
    }

    std_mutex_unlock ( &tk_fiber_state->acquired_threads_mutex );

    tk_fiber_thread_entry_point ( NULL );

    std_mutex_lock ( &tk_fiber_state->acquired_threads_mutex );
    acquired_thread->thread_handle = std_thread_null_handle_m;
    tk_release_condition_b release_cause = acquired_thread->release_cause;
    std_list_push ( tk_fiber_state->acquired_threads_freelist, acquired_thread );
    std_mutex_unlock ( &tk_fiber_state->acquired_threads_mutex );

    return release_cause;
}

static void release_acquired_thread ( tk_acquired_thread_t* acquired_thread, tk_release_condition_b cause ) {
    tk_fiber_thread_context_t* thread_context = &tk_fiber_state->thread_contexts_array[std_thread_index ( acquired_thread->thread_handle )];
    acquired_thread->release_cause = cause;
    thread_context->join_flag = true;
}

static bool check_for_all_workloads_done() {
    return std_queue_shared_used_size ( &tk_fiber_state->free_workloads ) == tk_max_parallel_tasks_m * sizeof ( uint32_t );
}

static void acquired_threads_on_all_workloads_done () {
    std_mutex_lock ( &tk_fiber_state->acquired_threads_mutex );

    for ( size_t i = 0; i < tk_max_threads_m; ++i ) {
        tk_acquired_thread_t* acquired_thread = &tk_fiber_state->acquired_threads_array[i];

        if ( acquired_thread->thread_handle != std_thread_null_handle_m && ( acquired_thread->release_condition & tk_release_condition_all_workloads_done_m ) ) {
            release_acquired_thread ( acquired_thread, tk_release_condition_all_workloads_done_m );
        }
    }

    std_mutex_unlock ( &tk_fiber_state->acquired_threads_mutex );
}

void tk_fiber_thread_release ( const tk_acquired_thread_h* threads, size_t count ) {
    std_mutex_lock ( &tk_fiber_state->acquired_threads_mutex );

    for ( size_t i = 0; i < count; ++i ) {
        tk_acquired_thread_t* acquired_thread = &tk_fiber_state->acquired_threads_array[threads[i]];
        release_acquired_thread ( acquired_thread, tk_release_condition_user_request_m );
    }

    std_mutex_unlock ( &tk_fiber_state->acquired_threads_mutex );
}

void tk_fiber_thread_release_all ( void ) {
    std_mutex_lock ( &tk_fiber_state->acquired_threads_mutex );

    for ( size_t i = 0; i < tk_max_threads_m; ++i ) {
        tk_acquired_thread_t* acquired_thread = &tk_fiber_state->acquired_threads_array[i];

        if ( acquired_thread->thread_handle != std_thread_null_handle_m ) {
            release_acquired_thread ( acquired_thread, tk_release_condition_user_request_m );
        }
    }

    std_mutex_unlock ( &tk_fiber_state->acquired_threads_mutex );
}

void tk_fiber_scheduler_stop ( void ) {
    tk_fiber_state->join_flag = true;

    for ( size_t i = 0; i < tk_fiber_state->thread_count; ++i ) {
        std_thread_join ( tk_fiber_state->threads_array[i] );
        // TODO free stack allocation on linux?
    }
}

// ------------------------------------------------------------------------------------------------
