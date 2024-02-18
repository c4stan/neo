#pragma once



// ------------------------------------------------------------------------------------------------
// Surface user API.

void                tk_worker_init ( void );

void                tk_worker_thread_pool_init_fixed ( size_t count, uint64_t core_mask );
void                tk_worker_thread_pool_init_default ( void );

tk_release_cause_e  tk_worker_thread_this_acquire ( tk_release_condition_e release_condition, tk_acquired_thread_h* out_handle );
void                tk_worker_thread_release ( const tk_acquired_thread_h* threads, size_t count );
void                tk_worker_thread_release_all ( void );

tk_workload_h       tk_worker_workload_schedule ( const tk_task_t* tasks, size_t tasks_count );
void                tk_worker_workload_wait ( tk_workload_h workload );
void                tk_worker_workload_wait_idle ( tk_workload_h workload );

void                tk_worker_scheduler_stop ( void );

// ------------------------------------------------------------------------------------------------
