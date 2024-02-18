// Header
#include <std_mutex.h>

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
void std_rwmutex_init ( std_rwmutex_t* mutex ) {
#if defined(std_platform_win32_m)
    InitializeSRWLock ( &mutex->os );
#elif defined(std_platform_linux_m)
    pthread_rwlock_init ( &mutex->os, NULL );
#endif
}

void std_rwmutex_lock_shared ( std_rwmutex_t* mutex ) {
#if defined(std_platform_win32_m)
    AcquireSRWLockShared ( &mutex->os );
#elif defined(std_platform_linux_m)
    pthread_rwlock_rdlock ( &mutex->os );
#endif
}

void std_rwmutex_lock_exclusive ( std_rwmutex_t* mutex ) {
#if defined(std_platform_win32_m)
    AcquireSRWLockExclusive ( &mutex->os );
#elif defined(std_platform_linux_m)
    pthread_rwlock_wrlock ( &mutex->os );
#endif
}

void std_rwmutex_unlock_shared ( std_rwmutex_t* mutex ) {
#if defined(std_platform_win32_m)
    ReleaseSRWLockShared ( &mutex->os );
#elif defined(std_platform_linux_m)
    pthread_rwlock_unlock ( &mutex->os );
#endif
}

void std_rwmutex_unlock_exclusive ( std_rwmutex_t* mutex ) {
#if defined(std_platform_win32_m)
    ReleaseSRWLockExclusive ( &mutex->os );
#elif defined(std_platform_linux_m)
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
