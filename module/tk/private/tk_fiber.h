#pragma once

#include <tk.h>

#include <std_queue.h>
#include <std_allocator.h>
#include <std_mutex.h>

// ------------------------------------------------------------------------------------------------

// Flow
//  - tk should get at some point loaded by (usually) the main thread.
//  - At that point the used should be able to specify how many threads tk should create for its own
//    task scheduling, along with allowing the user to register his own threads to the scheduler as
//    additional workers.
//  - At some point, tk's scheduler that it's time to release its workers. At that point, all its
//    personal threads are joined and released, and the external threads shall return to some calling
//    point.
//
//  Example:
//    [main thread]
//      tk_i* tk = std_module_get_m ( tk_module_name_m );
//      // One between...
//      // tk->init_fixed_thread_pool ( 6 );
//      // tk->init_optimal_thread_pool();
//      tk_task_t entry_point_task = ...;
//      tk->schedule_work ( &entry_point_task, 1 );
//      tk_suspend_message_t msg = tk->acquire_this_thread();
//      // will resume execution once someone calls tk->suspend()
//      // msg will contain info about why it stoppen
//      tk->resume()
//      msg = tk->acquire_this_thread()
//      // will resume execution once someone calls tk->suspend() again
//

// ------------------------------------------------------------------------------------------------
// Internal data types used by the scheduler.
//
// - A wait list is a linked list of fibers that are waiting on a workload to finish before they are allowed to run.
//
//   tk_fiber_workload_t --------> tk_fiber_context_t --> tk_fiber_context_t --> ... --> tk_fiber_context_t
//
//   tk_fiber_paused_context_t --> tk_fiber_context_t                                   (self)
//                           |
//                           '---> tk_fiber_workload_t --> tk_fiber_context_t -> ...    (pausing workload wait list)
//
// - Wait lists are intrusive and the wait_list_t type only holds the head of the list and a generation value
//   that has to match the one on the user handle of the workload that is being waited on. When all tasks under a
//   workload end, the gen value on the wait list head is increased. When appending a fiber to the wait list head,
//   a compare and swap ensures that the gen value on the list is the same as the one on the workload handle.
//   This means that after closing a wait list no other fiber can be put to wait on that workload, ensuring that
//   there are no waits on workloads that have already ended.
//
//   - on Wait on workload -            - on workload end -
//   ...                                Inc wait list gen
//   CAS on wait list gen               wake up all list entries ...
//   (implicit insert)
//   (implicit put fiber to sleep)
//
//   The CAS on inser happens at the very end of the procedure to put a fiber to sleep. The Inc on gen happens
//   at the very beginning of ending a workload and waking up all waiting fibers.
//
typedef union {
    uint64_t     u64;
    struct {
        uint32_t head;
        uint32_t gen;
    };
} tk_fiber_wait_list_t;

#define tk_fiber_idx_null_m UINT32_MAX

#if defined(std_platform_win32_m)
    // OS handle
    typedef uint64_t tk_fiber_h;
#endif

// Handle to a workload, protected by a gen field. On workload end, gen is invalidated, preventing stale access.
typedef union {
    uint64_t u64;
    struct {
        uint32_t idx;
        uint32_t gen;
    };
} tk_fiber_workload_h;

// To keep track of how many tasks in the workload are yet to be finished executing and of who is waiting on the workload.
// On workload completion, the wait list is invalidated (++gen) and fibers waiting on it are woken up.
typedef struct {
    uint32_t                remaining_tasks_count;
    tk_fiber_wait_list_t    wait_list;
} tk_fiber_workload_t;

// Fiber related data, including the intrusive wait list
typedef struct {
    uint32_t    id;             // unused?
    uint32_t    wait_list_next; // the intrusive wait list
    // TODO store the registers directly and swap context via inline asm
#if defined(std_platform_win32_m)
    tk_fiber_h  os_handle;
#elif defined(std_platform_linux_m)
    ucontext_t  os_context;
    void*       stack;
#endif
} tk_fiber_context_t;

// Individual task that is part of a workload
typedef struct {
    tk_task_t               task;
    tk_fiber_workload_t*    workload;
} tk_fiber_dispatched_task_t;

// State associated with a fiber that is waiting for another workload to end
typedef struct {
    tk_fiber_context_t*     self;
    tk_fiber_workload_h     pausing_workload;
} tk_fiber_paused_context_t;

// Thread local state tracking.
// When the current fiber gets paused, prev_fiber gets updated, then the fiber gets swapped out,
// and then shortly after the fiber is handed to the scheduler by the new fiber running on that same thread.
typedef struct {
    // Fiber created from the worker thread. Not really used to do any work, just need to swap back to it before exiting fiber-mode in win32
    // The wait list and id fields are unused.
    tk_fiber_context_t          main_fiber;
    // Pointer to current fiber context, is set right before swapping to it
    tk_fiber_context_t*         current_fiber;
    // Temp. storage for the paused fiber
    tk_fiber_paused_context_t   prev_fiber;
    // Only used by acquired threads ... TODO change that?
    bool                        join_flag;
} tk_fiber_thread_context_t;

// ------------------------------------------------------------------------------------------------
// These are simple system primitives call wraps.
/*
// Switch current fiber with another (change stack pointer register, ...)
void        tk_fiber_switch             ( tk_fiber_h os_handle );

// Create new fiber object
tk_fiber_h  tk_fiber_create             ( bool* join_flag, void ( routine ) ( void* ) );

// Get current fiber object handle
tk_fiber_h  tk_fiber_this_get           ( void );

// Enter in 'fiber-mode'
tk_fiber_h  tk_fiber_enter_from_thread  ( void );

// Exit from 'fiber-mode'
void        tk_fiber_exit_to_thread     ( void );
*/
// ------------------------------------------------------------------------------------------------
// Cooperative scheduling internal API.
// TODO merge (some of) this more with the actual scheduler, make it its explicit api instead of doing hidden accesses to the static scheduler object.

// Yield current fiber to some other waiting to execute.
//void tk_fiber_this_yield ( void );

// Yield current fiber to specific fiber
void tk_fiber_yield_to ( tk_fiber_context_t* context );

// Transition the current fiber to be waiting on another fiber to execute before running again.
void tk_fiber_wait_for ( tk_fiber_workload_h workload );

// Bookkeping stuff necessary when a fiber is getting executed again after having yelded.
void tk_fiber_on_context_return ( void );

// Get thread local context
//tk_fiber_thread_context_t* tk_fiber_this_get_thread_context ( void );

// Signal a waiting fiber that one of its dependencies is done running.
// Might cause the waiting fiber to transition from waiting on dependency to waiting to be executed.
void tk_fiber_on_task_completed ( const tk_fiber_dispatched_task_t* dispatch );

// Move a fiber context into the scheduler's wake_up_queue, where it waits to be executed by a thread.
void tk_fiber_wake_up ( tk_fiber_context_t* context );

// Add a fiber context (already wrapped in a paused context) to its workload wait list
bool tk_fiber_wait_list_insert ( tk_fiber_paused_context_t* context );

// Pool back a fiber context to the scheduler free_fiber_contexts pool
void tk_fiber_pool_context ( tk_fiber_context_t* context );

// Pop a fiber context from the scheduler free_fiber_contexts pool
tk_fiber_context_t* tk_fiber_pop_context ( void );

// ------------------------------------------------------------------------------------------------
// Scheduler



// ------------------------------------------------------------------------------------------------
// Entry point.



// ------------------------------------------------------------------------------------------------
// Surface API.

void                tk_fiber_init ( void );

void                tk_fiber_thread_pool_init ( const tk_thread_pool_params_t* params );

//
// Exposing an acquire API is dangerous because issues can occur. For example, if the user leds a thread while there are tasks being
// executed, and it's responsibility of the last task to release all current threads(?), then it could happen that all tasks are finished
// by the time the thread is actually acquired, and so this thread will remail acquired forever.
// On the other hand in order to support situations where TK owns all threads while executing work, it is necessary to schedule tasks before
// acquiring the last user thread, since once the thread is acquired the user cannot schedule any more work, so this situation cannot be avoided.
// One solution is to pass the tasks to schedule to the acquire call and handle this internally somehow.
// A better solution is probably to allow passing in an automatic release condition, e.g. ON_ALL_WRKLOADS_DONE, and let the scheduler release
// the threads when all workloads are completed. TODO do this
//
tk_release_condition_b  tk_fiber_thread_this_acquire ( tk_release_condition_b release_condition, tk_acquired_thread_h* out_handle );
void                    tk_fiber_thread_release ( const tk_acquired_thread_h* threads, size_t count );
void                    tk_fiber_thread_release_all ( void );

tk_workload_h       tk_fiber_workload_schedule ( const tk_task_t* tasks, size_t tasks_count );
void                tk_fiber_workload_wait ( tk_workload_h workload );
void                tk_fiber_workload_wait_idle ( tk_workload_h workload );

//void                tk_fiber_scheduler_pause ( void );
//void                tk_fiber_scheduler_resume ( void );
//void                tk_fiber_scheduler_start ( void );
void                tk_fiber_scheduler_stop ( void );

// ------------------------------------------------------------------------------------------------

typedef struct {
    void* _pad0;
    std_thread_h thread_handle; // Keep this out of the first 8 bytes to be able to iterate on the freelist
} tk_owned_thread_t;

typedef struct {
    tk_release_condition_b release_condition;
    tk_release_condition_b release_cause;
    std_thread_h thread_handle; // Keep this out of the first 8 bytes to be able to iterate on the freelist
} tk_acquired_thread_t;

typedef struct {
    std_thread_h*               threads_array;
    tk_fiber_thread_context_t*  thread_contexts_array;
    size_t                      thread_count;
    tk_fiber_context_t*         fiber_contexts_array;
    tk_fiber_workload_t*        workload_array;

    tk_acquired_thread_t*       acquired_threads_array;
    tk_acquired_thread_t*       acquired_threads_freelist;
    std_mutex_t                 acquired_threads_mutex;

    std_queue_shared_t          dispatch_queue;         // tk_fiber_dispatched_task_t
    std_queue_shared_t          ready_fiber_contexts;   // uint32_t to fiber_contexts
    std_queue_shared_t          free_fiber_contexts;    // uint32_t to fiber_contexts
    std_queue_shared_t          free_workloads;         // uint32_t to workloads

    bool                        join_flag;
} tk_fiber_state_t;

void tk_fiber_load ( tk_fiber_state_t* state );
void tk_fiber_reload ( tk_fiber_state_t* state );
void tk_fiber_unload ( void );
