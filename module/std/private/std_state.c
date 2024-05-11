#include <std_module.h>

#include "std_state.h"

// Will contain state address on app memory and null (zero) on dll memory.
static std_runtime_state_t* std_runtime_state_ptr;

void std_init ( int argc, char** argv ) {
    std_log_boot();

#if 0
    // TODO double check that this isn't needed
#if defined(std_platform_linux_m)
    {
        struct rlimit rl = {0};
        rl.rlim_cur = std_thread_stack_size_m;
        int result = setrlimit ( RLIMIT_STACK, &rl );
        std_assert_m ( result == 0 );
    }
#endif
#endif

    std_allocator_boot();

    std_runtime_state_t* state = std_virtual_heap_alloc_m ( std_runtime_state_t );

    std_allocator_init ( &state->allocator_state );
    std_log_init ( &state->log_state );
    std_platform_init ( &state->platform_state );
    std_time_init ( &state->time_state );
    std_process_init ( &state->process_state, argv, ( size_t ) argc );
    std_thread_init ( &state->thread_state );
    std_module_init ( &state->module_state );

    std_runtime_bind ( state );

    // TODO pass these to std_process_init?
    //std_process_set_args ( ( const char** ) argv, ( size_t ) argc );
}

size_t std_runtime_size ( void ) {
    return sizeof ( std_runtime_state_t );
}

void* std_runtime_state ( void ) {
    return std_runtime_state_ptr;
}

void std_runtime_bind ( void* std_runtime ) {
    std_runtime_state_ptr = std_runtime;
    std_allocator_attach ( &std_runtime_state_ptr->allocator_state );
    std_platform_attach ( &std_runtime_state_ptr->platform_state );
    std_log_attach ( &std_runtime_state_ptr->log_state );
    std_time_attach ( &std_runtime_state_ptr->time_state );
    std_process_attach ( &std_runtime_state_ptr->process_state );
    std_thread_attach ( &std_runtime_state_ptr->thread_state );
    std_module_attach ( &std_runtime_state_ptr->module_state );
}

void std_shutdown ( void ) {
    std_module_shutdown();
    std_thread_shutdown();
    std_process_shutdown();
    std_virtual_heap_free ( std_runtime_state_ptr );
}
