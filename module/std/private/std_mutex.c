// Header
#include <std_mutex.h>
#include <std_log.h>

#if defined(std_platform_win32_m)
    #include <synchapi.h>
#endif

#include <std_atomic.h>
#include <std_thread.h>

void std_mutex_init ( std_mutex_t* mutex ) {
#if defined(std_platform_win32_m)
    InitializeCriticalSection ( &mutex->os );
#elif defined(std_platform_linux_m)
    pthread_mutex_init ( &mutex->os, NULL );
#endif
}

void std_mutex_lock ( std_mutex_t* mutex ) {
#if defined(std_platform_win32_m)
    EnterCriticalSection ( &mutex->os );
#elif defined(std_platform_linux_m)
    pthread_mutex_lock ( &mutex->os );
#endif
}

void std_mutex_unlock ( std_mutex_t* mutex ) {
#if defined(std_platform_win32_m)
    LeaveCriticalSection ( &mutex->os );
#elif defined(std_platform_linux_m)
    pthread_mutex_unlock ( &mutex->os );
#endif
}

void std_mutex_deinit ( std_mutex_t* mutex ) {
#if defined(std_platform_win32_m)
    DeleteCriticalSection ( &mutex->os );
#elif defined(std_platform_linux_m)
    pthread_mutex_destroy ( &mutex->os );
#endif
}

// TODO FIBER_SAFE
// TODO on linux pthread_rwlock is not reentrant, meaning a thread owning a write lock can't take a read lock :/ this breaks a bunch of things.
void std_rwmutex_init ( std_rwmutex_t* mutex ) {
#if defined(std_platform_win32_m)
    InitializeSRWLock ( &mutex->os );
    mutex->write_thread = std_thread_null_handle_m;
    mutex->write_thread_count = 0;
#elif defined(std_platform_linux_m)
    pthread_rwlock_init ( &mutex->os, NULL );
#endif
}

void std_rwmutex_lock_read ( std_rwmutex_t* mutex ) {
#if defined(std_platform_win32_m)
    std_thread_h thread = std_thread_this();

    if ( thread == std_thread_this() ) {
        return;
    }

    AcquireSRWLockShared ( &mutex->os );
#elif defined(std_platform_linux_m)
    std_log_info_m ( "lock read" );
    pthread_rwlock_rdlock ( &mutex->os );
#endif
}

void std_rwmutex_lock_write ( std_rwmutex_t* mutex ) {
#if defined(std_platform_win32_m)
    std_thread_h thread = std_thread_this();
    
    if ( thread == mutex->write_thread ) {
        ++mutex->write_thread_count;
        return;
    }

    AcquireSRWLockExclusive ( &mutex->os );

    mutex->write_thread = thread;
    mutex->write_thread_count = 1;
#elif defined(std_platform_linux_m)
    std_log_info_m ( "lock write" );
    pthread_rwlock_wrlock ( &mutex->os );
#endif
}

void std_rwmutex_unlock_read ( std_rwmutex_t* mutex ) {
#if defined(std_platform_win32_m)
    std_thread_h thread = std_thread_this();

    if ( thread == std_thread_this() ) {
        return;
    }
    
    ReleaseSRWLockShared ( &mutex->os );
#elif defined(std_platform_linux_m)
    std_log_info_m ( "unlock read" );
    pthread_rwlock_unlock ( &mutex->os );
#endif
}

void std_rwmutex_unlock_write ( std_rwmutex_t* mutex ) {
#if defined(std_platform_win32_m)
    if ( --mutex->write_thread_count ) {
        return;
    }

    mutex->write_thread = std_thread_null_handle_m;

    ReleaseSRWLockExclusive ( &mutex->os );
#elif defined(std_platform_linux_m)
    std_log_info_m ( "unlock write" );
    pthread_rwlock_unlock ( &mutex->os );
#endif
}

void std_rwmutex_deinit ( std_rwmutex_t* mutex ) {
#if defined(std_platform_win32_m)
    // Win32 SRW locks don't need a destructor https://docs.microsoft.com/en-us/windows/desktop/api/synchapi/nf-synchapi-initializesrwlock
    std_unused_m ( mutex );
#elif defined(std_platform_linux_m)
    pthread_rwlock_destroy ( &mutex->os );
#endif
}

void std_spilock_init ( std_spinlock_t* spinlock ) {
    spinlock->state = 0;
}

void std_spinlock_lock ( std_spinlock_t* spinlock ) {
    uint32_t expected_read = 0;

cas:

    if ( std_compare_and_swap_u32 ( &spinlock->state, &expected_read, 1 ) ) {
        return;
    }

    while ( spinlock->state != 0 ) {
        std_thread_this_yield();
    }

    goto cas;
}

void std_spinlock_unlock ( std_spinlock_t* spinlock ) {
    spinlock->state = 0;
}
