#pragma once

// stdlib
#include <stdint.h>

// std
#include <std_compiler.h>
#include <std_platform.h>

/*
    TODO
    - remove the hidden fiber support and instead make explicit alternatives to thread-based sync primitives that use atomics and work with fibers.
      Let the user pick which one to use. MAYBE define macros where it makes sense to default to one or the other implementation.
    - add:
        - cond var
        - spinlock
*/

// Mutex
typedef struct {
#if defined(std_platform_win32_m)
    CRITICAL_SECTION os;
#elif defined(std_platform_linux_m)
    pthread_mutex_t os;
#endif
} std_mutex_t;

// RW Mutex
typedef struct {
#if defined(std_platform_win32_m)
    SRWLOCK os;
#elif defined(std_platform_linux_m)
    pthread_rwlock_t os;
#endif
} std_rwmutex_t;

typedef struct {
    uint32_t state; // 0 for free, 1 for busy
} std_spinlock_t;

// Mutex
void std_mutex_init    ( std_mutex_t* mutex );
void std_mutex_lock    ( std_mutex_t* mutex );
void std_mutex_unlock  ( std_mutex_t* mutex );
void std_mutex_deinit  ( std_mutex_t* mutex );

// RW Mutex
void std_rwmutex_init             ( std_rwmutex_t* mutex );
void std_rwmutex_lock_shared      ( std_rwmutex_t* mutex );
void std_rwmutex_lock_exclusive   ( std_rwmutex_t* mutex );
void std_rwmutex_unlock_shared    ( std_rwmutex_t* mutex );
void std_rwmutex_unlock_exclusive ( std_rwmutex_t* mutex );
void std_rwmutex_deinit           ( std_rwmutex_t* mutex );

// Spinlock
void std_spilock_init ( std_spinlock_t* spinlock );
void std_spinlock_lock ( std_spinlock_t* spinlock );
void std_spinlock_unlock ( std_spinlock_t* spinlock );
