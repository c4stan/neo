#pragma once

// stdlib
#include <stdint.h>

// std
#include <std_compiler.h>
#include <std_platform.h>
#include <std_thread.h>

/*
    TODO
    - Think about fiber support, can it be hidden away by just flipping a define or does it need explicit duplicate primitives that support fibers? should std have std_fiber, similar to std_thread? (possibly also enabled only when some define is flipped on)
    - add more primitives:
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
// Once a write lock is acquired, write and read locks can be called recursively
// Read only locks do NOT allow for further recursive calls to read or write locks
typedef struct {
#if defined(std_platform_win32_m)
    SRWLOCK os;
    std_thread_h write_thread;
    uint64_t write_thread_count;
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
void std_rwmutex_init           ( std_rwmutex_t* mutex );
void std_rwmutex_lock_read      ( std_rwmutex_t* mutex );
void std_rwmutex_lock_write     ( std_rwmutex_t* mutex );
void std_rwmutex_unlock_read    ( std_rwmutex_t* mutex );
void std_rwmutex_unlock_write   ( std_rwmutex_t* mutex );
void std_rwmutex_deinit         ( std_rwmutex_t* mutex );

// Spinlock
void std_spilock_init ( std_spinlock_t* spinlock );
void std_spinlock_lock ( std_spinlock_t* spinlock );
void std_spinlock_unlock ( std_spinlock_t* spinlock );
