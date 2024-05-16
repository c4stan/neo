#pragma once

/*
    Active dev happens on Win32.
    Occasionally, support for Linux is added.
    Adding support for mobile (Android) one day would be cool.
*/

// Detect platform
// TODO remove as many includes as possible from here and move them into the specific C files where they're needed
#if defined(_WIN32)
    #define std_platform_win32_m
    #define WIN32_LEAN_AND_MEAN
    #define _CRT_SECURE_NO_WARNINGS
    #include <Windows.h>
    #include <windowsx.h>
    #include <memoryapi.h>
    #include <DbgHelp.h>
    #include <synchapi.h>

    #include <stdint.h>
    #include <uchar.h>
    #include <stdbool.h>
    #include <stdarg.h>
    #include <intrin.h>
    #include <float.h>
    #include <inttypes.h>

    #if defined _WIN64
        #define std_cpu_x64_m
    #else
        #define std_cpu_x86_m
    #endif

#elif defined(__linux__)
    #define std_platform_linux_m

    #define _GNU_SOURCE
    #include <stdint.h>
    #include <uchar.h>
    #include <stdbool.h>
    #include <stdarg.h>
    #include <float.h>
    #include <inttypes.h>
    #include <string.h>
    #include <errno.h>

    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <sys/syscall.h>
    #include <sys/sysinfo.h>
    #include <sys/mman.h>
    #include <sys/resource.h>
    #include <pthread.h>
    #include <time.h>
    #include <sched.h>
    #include <stdio.h>
    #include <dlfcn.h>
    #include <signal.h>
    #include <stdlib.h>
    #include <fcntl.h>
    #include <sys/stat.h>

    #if defined(__x86_64__)
        #define std_cpu_x64_m
    #elif #defined (__i386__)
        #define std_cpu_x86_m
    #else
        #error "Unexpected CPU architecture detected on Linux platform"
    #endif

#else
    #error "Unsupported platform"
#endif
/*
#elif defined(__unix__)
    #define std_platform_unix_m
#elif defined(_POSIX_VERSION)
    #define std_platform_posix_m
#else
    #error "Unknown platform"
#endif
*/

#if UINTPTR_MAX == UINT32_MAX
    #define std_pointer_size_m 4
    #define std_size_wide_m    0
#elif UINTPTR_MAX == UINT64_MAX
    #define std_pointer_size_m 8
    #define std_size_wide_m    1
#else
    #error "Unexpected pointer size"
#endif

//==============================================================================

typedef enum {
    std_platform_cache_data_m,
    std_platform_cache_instruction_m,
    std_platform_cache_unified_m,
    std_platform_cache_type_unknown_m,
} std_platform_cache_type_e;

typedef enum {
    std_platform_cache_l1_m,
    std_platform_cache_l2_m,
    std_platform_cache_l3_m,
} std_platform_cache_level_e;

#define std_platform_cache_fully_associative_m SIZE_MAX;
typedef struct {
    uint64_t                    logical_cores_mask;
    std_platform_cache_type_e   type;
    std_platform_cache_level_e  level;
    size_t                      size;
    size_t                      line_size;
    size_t                      associativity;
} std_platform_cache_info_t;

typedef struct {
    uint64_t    logical_cores_mask;
    //size_t      processor_idx;
} std_platform_physical_core_info_t;

typedef struct {
    size_t      physical_core_idx;
} std_platform_logical_core_info_t;

typedef enum {
    std_platform__mx86,
    std_platform_amd64_m,
    std_platform_architecture_unknown_m
} std_platform_architecture_e;

typedef struct {
    size_t      total_ram_size;
    size_t      total_swap_size;
    size_t      virtual_page_size;
    //void*       lowest_user_memory_address;
    //void*       highest_user_memory_address;
} std_platform_memory_info_t;

typedef enum {
    std_platform_core_relationship_same_processor_m,
    std_platform_core_relationship_same_physical_core_m,
    std_platform_core_relationship_error_m
} std_platform_core_relationship_e;

//==============================================================================

size_t                          std_platform_caches_info ( std_platform_cache_info_t* info, size_t cap );
size_t                          std_platform_physical_cores_info ( std_platform_physical_core_info_t* info, size_t cap );
size_t                          std_platform_logical_cores_info ( std_platform_logical_core_info_t* info, size_t cap );
std_platform_memory_info_t      std_platform_memory_info ( void );

// Utility to query relationship between two logical cores.
// TODO Should this exist? Getting this info out of the cores info buffers is pretty easy already.
std_platform_core_relationship_e   std_platform_core_relationship ( uint64_t core_idx, uint64_t other_core_idx );

//==============================================================================
