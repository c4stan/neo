#include <std_thread.h>
#include <std_platform.h>
#include <std_atomic.h>

#include "std_state.h"

#if defined(std_platform_linux_m)
    #include <pthread.h>
#endif

#if defined (std_platform_win32_m)
    #include <processthreadsapi.h>
#endif

//==============================================================================

static std_thread_state_t* std_thread_state;

//==============================================================================

#if defined(std_platform_win32_m)
static DWORD WINAPI std_thread_launcher ( LPVOID param ) {
    // lock unlock to ensure execution starts after the thread is fully initialized
    std_mutex_lock ( &std_thread_state->mutex );
    std_mutex_unlock ( &std_thread_state->mutex );

    std_thread_t* thread = ( std_thread_t* ) param;
    std_verify_m ( TlsSetValue ( ( DWORD ) std_thread_state->tls_alloc, ( LPVOID ) thread->handle.u64 ) == TRUE );
    thread->routine ( thread->arg );
    return 0;   // Don't care about letting the OS know what happened at thread runtime
}
#elif defined(std_platform_linux_m)
static void* std_thread_launcher ( void* param ) {
    // lock unlock to ensure execution starts after the thread is fully initialized
    std_mutex_lock ( &std_thread_state->mutex );
    std_mutex_unlock ( &std_thread_state->mutex );

    std_thread_t* thread = ( std_thread_t* ) param;
    // TODO test this
    std_verify_m ( pthread_setspecific ( ( pthread_key_t ) std_thread_state->tls_alloc, ( void* ) thread->handle.u64 ) == 0 );
    thread->routine ( thread->arg );
    return NULL;
}
#endif

// NOTE: this function uses the param state instead of the static one, and it cannot
// call to outside functions that expect a functioning std_xxx_state !
static void std_thread_register_main ( std_thread_state_t* state ) {
    // Lock
    // Initialize thread
    // Increase threads pop
    std_mutex_lock ( &state->mutex );

    std_thread_t* thread = std_list_pop_m ( &state->threads_freelist );
    std_assert_m ( thread != NULL );

#if defined(std_platform_win32_m)
    thread->os_id = ( uint64_t ) GetCurrentThreadId();
    HANDLE os_handle = GetCurrentThread();
    thread->os_handle = ( uint64_t ) os_handle;
#elif defined(std_platform_linux_m)
    thread->os_id = ( uint64_t ) syscall ( SYS_gettid );
    pthread_t os_handle = pthread_self();
    thread->os_handle = ( uint64_t ) os_handle;
#endif
    thread->uid = state->uid++;
    thread->routine = NULL;
    thread->arg = NULL;
    std_str_copy ( thread->name, std_thread_name_max_len_m, std_pp_eval_string_m ( std_thread_main_thread_name_m ) );

#if defined(std_platform_win32_m)
    ULONG_PTR stack_high;
    ULONG_PTR stack_low;
    GetCurrentThreadStackLimits ( &stack_low, &stack_high );
    thread->stack_size = stack_high - stack_low;
#elif defined(std_platform_linux_m)
    thread->stack_size = std_thread_stack_size_m;
#endif
    std_assert_m ( thread->stack_size == std_thread_stack_size_m );
    thread->core_mask = std_thread_main_thread_core_mask_m;

#if defined(std_platform_linux_m)
    thread->is_alive = true;
#endif

    // Unlock
    // Set OS core affinity mask if specified
    // Set TLS thread handle
    std_mutex_unlock ( &state->mutex );

    if ( std_thread_main_thread_core_mask_m != std_thread_core_mask_any_m ) {
#if defined(std_platform_win32_m)
        SetThreadAffinityMask ( os_handle, std_thread_main_thread_core_mask_m );
#elif defined(std_platform_linux_m)

#if CPU_SETSIZE < std_thread_max_threads_m
#error "CPU thread affinity mask doesn't fit current std_thread_max_threads_m value"
#endif
        cpu_set_t cpu_mask;
        CPU_ZERO ( &cpu_mask );
        CPU_SET ( std_thread_main_thread_core_mask_m, &cpu_mask );
        pthread_setaffinity_np ( os_handle, sizeof ( cpu_mask ), &cpu_mask );
#endif
    }
 
    if ( std_str_cmp ( thread->name, "" ) != 0 ) {
#if defined(std_platform_win32_m)
        WCHAR unicode_name[std_thread_name_max_len_m];
        MultiByteToWideChar ( CP_UTF8, 0, thread->name, -1, unicode_name, std_thread_name_max_len_m );
        SetThreadDescription ( os_handle, unicode_name );
#elif defined(std_platform_linux_m)
        pthread_setname_np ( os_handle, thread->name );
#endif
    }

    std_thread_h thread_handle = thread->handle;
    thread_handle.idx = thread - state->threads_array;
    thread->handle = thread_handle;

#if defined(std_platform_win32_m)
    std_verify_m ( TlsSetValue ( ( DWORD ) state->tls_alloc, ( LPVOID ) thread_handle.u64 ) == TRUE );
#elif defined(std_platform_linux_m)
    std_verify_m ( pthread_setspecific ( ( pthread_key_t ) state->tls_alloc, ( void* ) thread_handle.u64 ) == 0 );
#endif
}

//==============================================================================

void std_thread_init ( std_thread_state_t* state ) {
    state->threads_array = std_virtual_heap_alloc_array_m ( std_thread_t, std_thread_max_threads_m );
    std_thread_t* threads_array = state->threads_array;
    for ( uint64_t i = 0; i < std_thread_max_threads_m; ++i ) {
        threads_array[i].handle = std_null_handle_m ( std_thread_h );
    }

    state->threads_freelist = std_freelist_m ( state->threads_array, std_thread_max_threads_m );
    state->uid = 0;
#if defined(std_platform_win32_m)
    state->tls_alloc = TlsAlloc();
    std_assert_m ( state->tls_alloc != TLS_OUT_OF_INDEXES );
#elif defined(std_platform_linux_m)
    std_verify_m ( pthread_key_create ( ( pthread_key_t* ) &state->tls_alloc, NULL ) == 0 );
#endif
    std_mutex_init ( &state->mutex );
    std_thread_register_main ( state );
}

void std_thread_attach ( std_thread_state_t* state ) {
    std_thread_state = state;
}

void std_thread_shutdown ( void ) {
    std_mutex_deinit ( &std_thread_state->mutex );
    std_virtual_heap_free ( std_thread_state->threads_array );
}

//==============================================================================

// TODO only create userland thread data struct here, and create actual thread in thread_start call ?
// or maybe just do both things in one single API call?
std_thread_h std_thread ( std_thread_routine_f* routine, void* arg, const char* name, uint64_t core_mask ) {
    // Lock
    // Pop
    // Initialize idx, routine, arg, name, uid
    // Increate threads pop
    std_mutex_lock ( &std_thread_state->mutex );

    std_thread_t* thread = std_list_pop_m ( &std_thread_state->threads_freelist );
    std_assert_m ( thread != NULL );

    size_t idx = ( size_t ) ( thread - std_thread_state->threads_array );
    std_thread_h handle = thread->handle;
    handle.idx = idx;
    thread->handle = handle;
    thread->routine = routine;
    thread->arg = arg;
    std_str_copy ( thread->name, std_thread_name_max_len_m, name );
    uint32_t uid = std_thread_state->uid++;
    std_assert_m ( uid != 0 );

    thread->uid = uid;
    thread->stack_size = std_thread_stack_size_m;

    // Create OS thread
    // Set OS core affinity mask if specified
    uint32_t os_id;

    // Thread is created suspended
#if defined(std_platform_win32_m)
    HANDLE os_handle = CreateThread ( NULL, std_thread_stack_size_m, std_thread_launcher, thread, 0, ( LPDWORD ) &os_id );

    if ( core_mask != std_thread_core_mask_any_m ) {
        SetThreadAffinityMask ( os_handle, core_mask );
    }

    WCHAR unicode_name[std_thread_name_max_len_m];
    MultiByteToWideChar ( CP_UTF8, 0, thread->name, -1, unicode_name, std_thread_name_max_len_m );
    SetThreadDescription ( os_handle, unicode_name );

#elif defined(std_platform_linux_m)
    pthread_attr_t attributes;
    pthread_attr_init ( &attributes );
    pthread_attr_setstacksize ( &attributes, std_thread_stack_size_m );
    //pthread_attr_setname_np ( &attributes, thread->name );

    if ( core_mask != std_thread_core_mask_any_m ) {
        cpu_set_t cpu_mask;
        CPU_ZERO ( &cpu_mask );
        CPU_SET ( core_mask, &cpu_mask );
        // TODO linux
        //pthread_attr_setaffinity_np ( &attributes, sizeof ( cpu_mask ), &cpu_mask );
    }

    pthread_t os_handle;
    int result = pthread_create ( &os_handle, &attributes, std_thread_launcher, thread );
    std_assert_m ( result == 0 );
    os_id = os_handle; // TODO how to get tid from pthread handle? not sure if possible at all
    pthread_setname_np ( os_handle, thread->name );

    thread->is_alive = true;
#endif

    // Init core mask, OS handle, OS id
    thread->core_mask = core_mask;
    thread->os_handle = ( uint64_t ) os_handle;
    thread->os_id = os_id;

    // Unlock
    // Return handle
    std_mutex_unlock ( &std_thread_state->mutex );

    return handle;
}

bool std_thread_join ( std_thread_h thread_handle ) {
    std_thread_t* thread = &std_thread_state->threads_array[thread_handle.idx];
#if defined(std_platform_win32_m)
    DWORD result = WaitForSingleObject ( ( HANDLE ) thread->os_handle, INFINITE );
    std_verify_m ( result == WAIT_OBJECT_0 );
#elif defined(std_platform_linux_m)
    int result = pthread_join ( ( pthread_t ) thread->os_handle, NULL );
    std_verify_m ( result == 0 );
    thread->is_alive = false;
#endif
    ++thread->handle.gen;
    std_mutex_lock ( &std_thread_state->mutex );
    std_list_push ( &std_thread_state->threads_freelist, thread );
    std_mutex_unlock ( &std_thread_state->mutex );
    return true;
}

//==============================================================================

bool std_thread_alive ( std_thread_h thread_handle ) {
    std_thread_t* thread = &std_thread_state->threads_array[thread_handle.idx];
#if defined(std_platform_win32_m)
    DWORD result = WaitForSingleObject ( ( HANDLE ) thread->os_handle, 0 );

    if ( result == WAIT_TIMEOUT ) {
        return true;
    } else {
        return false;
    }
#elif defined(std_platform_linux_m)
    return thread->is_alive;
#endif
}

uint32_t std_thread_uid ( std_thread_h thread_handle ) {
    std_thread_t* thread = &std_thread_state->threads_array[thread_handle.idx];
    return thread->uid;
}

uint32_t std_thread_index ( std_thread_h thread_handle ) {
    return thread_handle.idx;
}

//==============================================================================

std_thread_h std_thread_this ( void ) {
    std_thread_h thread_handle;
#if defined(std_platform_win32_m)
    thread_handle = ( std_thread_h ) { .u64 = ( uint64_t ) TlsGetValue ( ( DWORD ) std_thread_state->tls_alloc ) };
#elif defined(std_platform_linux_m)
    thread_handle = ( std_thread_h ) { .u64 = ( uint64_t ) pthread_getspecific ( ( pthread_key_t ) std_thread_state->tls_alloc ) };
#endif
    return thread_handle;
}

void std_thread_this_yield ( void ) {
#if defined(std_platform_win32_m)
    YieldProcessor();
#elif defined(std_platform_linux_m)
    sched_yield();
#endif
}

void std_thread_this_sleep ( size_t milliseconds ) {
#if defined(std_platform_win32_m)
    Sleep ( ( DWORD ) milliseconds );
#elif defined(std_platform_linux_m)
    usleep ( milliseconds * 1000 );
#endif
}

void std_thread_this_exit ( void ) {
    // TODO cleanup?
#if defined(std_platform_win32_m)
    ExitThread ( 0 );
#elif defined(std_platform_linux_m)
    pthread_exit ( NULL );
#endif
}

//==============================================================================

bool std_thread_info ( std_thread_info_t* info, std_thread_h thread_handle ) {
    std_thread_t* thread = &std_thread_state->threads_array[thread_handle.idx];
    info->stack_size = thread->stack_size;
    info->core_mask = thread->core_mask;
    std_str_copy ( info->name, std_thread_name_max_len_m, thread->name );
    return true;
}

const char* std_thread_name ( std_thread_h thread_handle ) {
    std_thread_t* thread = &std_thread_state->threads_array[thread_handle.idx];
    return thread->name;
}

void std_thread_set_core_mask ( std_thread_h thread_handle, uint64_t core_mask ) {
    std_thread_t* thread = &std_thread_state->threads_array[thread_handle.idx];

#if defined(std_platform_win32_m)
    SetThreadAffinityMask ( ( HANDLE ) thread->os_handle, core_mask );
#else
    cpu_set_t cpu_mask;
    CPU_ZERO ( &cpu_mask );
    CPU_SET ( core_mask, &cpu_mask );
    pthread_setaffinity_np ( thread->os_handle, sizeof ( cpu_mask ), &cpu_mask );
#endif
}
