#include <std_atomic.h>

#include <std_platform.h>
#include <std_compiler.h>

#ifdef std_platform_win32_m
    #include <Windows.h>
    #include <intrin.h>
#endif

// ------------------------------------------------------------------------------------------------------
// Fences
// On x86 the instruction sequences load->load, load->store, store->store are all guaranteed to not
// be reordered by the hardware, so only the compiler has to be notified for these. However, a
// store->load can be reordered by hardware, so it has its own memory fence.
//
// https://docs.microsoft.com/en-us/windows/win32/dxtecharts/lockless-programming
// https://preshing.com/20120930/weak-vs-strong-memory-models/
//
// ------------------------------------------------------------------------------------------------------

void std_compiler_fence ( void ) {
    std_asm_m volatile ( "" : : : "memory" );
}

void std_memory_fence ( void ) {
#if defined(std_platform_win32_m)
    MemoryBarrier();
#elif defined(std_platform_linux_m)
    __sync_synchronize();
#endif
}

// ------------------------------------------------------------------------------------------------------
// CAS Operations
// ------------------------------------------------------------------------------------------------------

/*
bool std_compare_and_swap_ptr ( void* pointer_addr, void** expected_read, void* conditional_write ) {
    void* read;
#ifdef std_platform_win32_m
    read = _InterlockedCompareExchangePointer ( ( PVOID* ) pointer_addr, conditional_write, *expected_read );
#endif
    bool retval = read == *expected_read;
    *expected_read = read;
    return retval;
}
*/

bool std_compare_and_swap_i32 ( int32_t* atomic, int32_t* expected_read, int32_t conditional_write ) {
    int32_t read;
#if defined(std_platform_win32_m)
    read = _InterlockedCompareExchange ( ( long* ) atomic, conditional_write, *expected_read );
#elif defined(std_platform_linux_m)
    read = __sync_val_compare_and_swap_4 ( atomic, *expected_read, conditional_write );
#endif
    bool retval = read == *expected_read;
    *expected_read = read;
    return retval;
}

bool std_compare_and_swap_i64 ( int64_t* atomic, int64_t* expected_read, int64_t conditional_write ) {
    int64_t read;
#if defined(std_platform_win32_m)
    read = _InterlockedCompareExchange64 ( atomic, conditional_write, *expected_read );
#elif defined(std_platform_linux_m)
    read = __sync_val_compare_and_swap_8 ( atomic, *expected_read, conditional_write );
#endif
    bool retval = read == *expected_read;
    *expected_read = read;
    return retval;
}

bool std_compare_and_swap_u32 ( uint32_t* atomic, uint32_t* expected_read, uint32_t conditional_write ) {
    uint32_t read;
#if defined(std_platform_win32_m)
    read = ( uint32_t ) _InterlockedCompareExchange ( ( LONG* ) atomic, ( LONG ) conditional_write, ( LONG ) * expected_read );
#elif defined(std_platform_linux_m)
    read = ( uint32_t ) __sync_val_compare_and_swap_4 ( ( int32_t* ) atomic, ( int32_t ) * expected_read, ( int32_t ) conditional_write );
#endif
    bool retval = read == *expected_read;
    *expected_read = read;
    return retval;
}

bool std_compare_and_swap_u64 ( uint64_t* atomic, uint64_t* expected_read, uint64_t conditional_write ) {
    uint64_t read;
#if defined(std_platform_win32_m)
    read = ( uint64_t ) _InterlockedCompareExchange64 ( ( LONG64* ) atomic, ( LONG64 ) conditional_write, ( LONG64 ) * expected_read );
#elif defined(std_platform_linux_m)
    read = ( uint64_t ) __sync_val_compare_and_swap_8 ( ( uint64_t* ) atomic, ( uint64_t ) * expected_read, ( uint64_t ) conditional_write );
#endif
    bool retval = read == *expected_read;
    *expected_read = read;
    return retval;
}

bool std_compare_and_swap_ptr ( void* atomic, void* expected_read, void* conditional_write ) {
    return std_compare_and_swap_u64 ( ( uint64_t* ) atomic, ( uint64_t* ) expected_read, ( uint64_t ) conditional_write );
}

// ------------------------------------------------------------------------------------------------------
// Atomic Exchange
// ------------------------------------------------------------------------------------------------------

int32_t std_atomic_exchange_i32 ( int32_t* atomic, int32_t write ) {
#if defined(std_platform_win32_m)
    return _InterlockedExchange ( ( LONG* ) atomic, ( LONG ) write );
#elif defined(std_platform_linux_m)
    return __sync_lock_test_and_set_4 ( atomic, write );
#endif
}

int64_t std_atomic_exchange_i64 ( int64_t* atomic, int64_t write ) {
#if defined(std_platform_win32_m)
    return _InterlockedExchange64 ( ( LONG64* ) atomic, write );
#elif defined(std_platform_linux_m)
    return __sync_lock_test_and_set_8 ( atomic, write );
#endif
}

uint32_t std_atomic_exchange_u32 ( uint32_t* atomic, uint32_t write ) {
#if defined(std_platform_win32_m)
    return ( uint32_t ) _InterlockedExchange ( ( LONG* ) atomic, ( LONG ) write );
#elif defined(std_platform_linux_m)
    return ( uint32_t ) __sync_lock_test_and_set_4 ( ( int32_t* ) atomic, ( int32_t ) write );
#endif
}

uint64_t std_atomic_exchange_u64 ( uint64_t* atomic, uint64_t write ) {
#if defined(std_platform_win32_m)
    return ( uint64_t ) _InterlockedExchange64 ( ( LONG64* ) atomic, ( LONG64 ) write );
#elif defined(std_platform_linux_m)
    return ( uint64_t ) __sync_lock_test_and_set_8 ( ( int64_t* ) atomic, ( int64_t ) write );
#endif
}

// ------------------------------------------------------------------------------------------------------
// Atomic Increment/Decrement
// ------------------------------------------------------------------------------------------------------

int32_t std_atomic_increment_i32 ( int32_t* atomic ) {
#if defined(std_platform_win32_m)
    return _InterlockedIncrement ( ( long* ) atomic );
#elif defined(std_platform_linux_m)
    return __sync_add_and_fetch_4 ( atomic, 1 );
#endif
}

int64_t std_atomic_increment_i64 ( int64_t* atomic ) {
#if defined(std_platform_win32_m)
    return _InterlockedIncrement64 ( atomic );
#elif defined(std_platform_linux_m)
    return __sync_add_and_fetch_8 ( atomic, 1 );
#endif
}

uint32_t std_atomic_increment_u32 ( uint32_t* atomic ) {
#if defined(std_platform_win32_m)
    return ( uint32_t ) _InterlockedIncrement ( ( LONG* ) atomic );
#elif defined(std_platform_linux_m)
    return __sync_add_and_fetch_4 ( ( int32_t* ) atomic, 1 );
#endif
}

uint64_t std_atomic_increment_u64 ( uint64_t* atomic ) {
#if defined(std_platform_win32_m)
    return ( uint64_t ) _InterlockedIncrement64 ( ( LONG64* ) atomic );
#elif defined(std_platform_linux_m)
    return __sync_add_and_fetch_8 ( ( int64_t* ) atomic, 1 );
#endif
}

int32_t std_atomic_decrement_i32 ( int32_t* atomic ) {
#if defined(std_platform_win32_m)
    return _InterlockedDecrement ( ( LONG* ) atomic );
#elif defined(std_platform_linux_m)
    return __sync_sub_and_fetch_4 ( atomic, 1 );
#endif
}

int64_t std_atomic_decrement_i64 ( int64_t* atomic ) {
#if defined(std_platform_win32_m)
    return _InterlockedDecrement64 ( atomic );
#elif defined(std_platform_linux_m)
    return __sync_sub_and_fetch_8 ( atomic, 1 );
#endif
}

uint32_t std_atomic_decrement_u32 ( uint32_t* atomic ) {
#if defined(std_platform_win32_m)
    return ( uint32_t ) _InterlockedDecrement ( ( LONG* ) atomic );
#elif defined(std_platform_linux_m)
    return __sync_sub_and_fetch_8 ( ( int64_t* ) atomic, 1 );
#endif
}

uint64_t std_atomic_decrement_u64 ( uint64_t* atomic ) {
#if defined(std_platform_win32_m)
    return ( uint64_t ) _InterlockedDecrement64 ( ( LONG64* ) atomic );
#elif defined(std_platform_linux_m)
    return __sync_sub_and_fetch_8 ( ( int64_t* ) atomic, 1 );
#endif
}

// ------------------------------------------------------------------------------------------------------
// Atomic fetch add
// ------------------------------------------------------------------------------------------------------

int32_t std_atomic_fetch_add_i32 ( int32_t* atomic, int32_t value_to_add ) {
#if defined(std_platform_win32_m)
    return ( int32_t ) _InterlockedExchangeAdd ( ( long* ) atomic, ( long ) value_to_add );
#elif defined(std_platform_linux_m)
    return __sync_fetch_and_add_4 ( atomic, value_to_add );
#endif
}

int64_t std_atomic_fetch_add_i64 ( int64_t* atomic, int64_t value_to_add ) {
#if defined(std_platform_win32_m)
    return ( int64_t ) _InterlockedExchangeAdd64 ( ( LONG64* ) atomic, ( LONG64 ) value_to_add );
#elif defined(std_platform_linux_m)
    return __sync_fetch_and_add_8 ( atomic, value_to_add );
#endif
}

uint32_t std_atomic_fetch_add_u32 ( uint32_t* atomic, uint32_t value_to_add ) {
#if defined(std_platform_win32_m)
    return ( uint32_t ) _InterlockedExchangeAdd ( ( long* ) atomic, ( long ) value_to_add );
#elif defined(std_platform_linux_m)
    return ( uint32_t ) __sync_fetch_and_add_4 ( ( int32_t* ) atomic, ( int32_t ) value_to_add );
#endif
}

uint64_t std_atomic_fetch_add_u64 ( uint64_t* atomic, uint64_t value_to_add ) {
#if defined(std_platform_win32_m)
    return ( uint64_t ) _InterlockedExchangeAdd64 ( ( LONG64* ) atomic, ( LONG64 ) value_to_add );
#elif defined(std_platform_linux_m)
    return ( uint64_t ) __sync_fetch_and_add_8 ( ( int64_t* ) atomic, ( int64_t ) value_to_add );
#endif
}
