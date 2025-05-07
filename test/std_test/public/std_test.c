#include <std_main.h>
#include <std_log.h>
#include <std_allocator.h>
#include <std_process.h>
#include <std_module.h>
#include <std_queue.h>
#include <std_time.h>
#include <std_hash.h>
#include <std_sort.h>
#include <std_file.h>

#include <stdio.h>
#include <stdlib.h>

#define CHILD_PROCESS_MAGIC_NUMBER "3735927486"
#define CHILD_PROCESS_OUTPUT "child process"
#define PARENT_PROCESS_MESSAGE "parent process"

std_warnings_ignore_m ( "-Wunused-variable" )
std_warnings_ignore_m ( "-Wunused-function" )

static uint64_t xorshift64star ( void ) {
    static uint64_t x = 1; /* initial seed must be nonzero, don't use a static variable for the state if multithreaded */
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    return x * 0x2545F4914F6CDD1DULL;
}

static float xorshift_to_f32 ( uint64_t xs ) {
    return ( ( float ) ( xs ) ) / ( float ) UINT64_MAX;
}

#if defined(std_platform_win32_m)
static void bench_virtual_heap ( void ) {
    std_log_info_m ( "benching std_virtual_heap..." );

#if 1
#if 0
    void* allocs1[13];
    std_alloc_t allocs2[13];

    SYSTEM_INFO si;
    GetSystemInfo ( &si );

    int k = 0;
    {
        std_tick_t t1 = std_tick_now();

        for ( size_t j = 0; j < 100; ++j ) {
            for ( size_t i = 0; i < 13; ++i ) {
                allocs1[i] = malloc ( si.dwPageSize );
            }
        }

        std_tick_t t2 = std_tick_now();

        for ( size_t j = 0; j < 100; ++j ) {
            for ( size_t i = 0; i < 13; ++i ) {
                allocs2[i] = std_virtual_heap_alloc ( si.dwPageSize, 8 );
            }
        }

        std_tick_t t3 = std_tick_now();

        double d1 = std_tick_to_micro_f64 ( t2 - t1 );
        double d2 = std_tick_to_micro_f64 ( t3 - t2 );
        std_log_info_m ( "[virtual page size alloc] malloc:" std_fmt_f64_m " std:" std_fmt_f64_m, d1, d2 );
    }
    {
        size_t alloc_sizes[] = {
            4256,
            256,
            4536217,
            2565,
            2565,
            2435327,
            2234,
            33455,
            2346217,
            314534,
            2357613,
            4351,
            3425437,
        }; // 13
        std_tick_t t1 = std_tick_now();

        for ( size_t j = 0; j < 100; ++j ) {
            for ( size_t i = 0; i < 13; ++i ) {
                allocs1[i] = malloc ( alloc_sizes[i] );
                ( ( volatile int* ) allocs1[i] ) [0] = ( ( int* ) allocs1[i] ) [0];
            }
        }

        std_tick_t t2 = std_tick_now();

        for ( size_t j = 0; j < 100; ++j ) {
            for ( size_t i = 0; i < 13; ++i ) {
                allocs2[i] = std_virtual_heap_alloc ( alloc_sizes[i], 8 );
                ( ( volatile int* ) allocs1[i] ) [0] = ( ( int* ) allocs1[i] ) [0];
            }
        }

        std_tick_t t3 = std_tick_now();

        double d1 = std_tick_to_micro_f64 ( t2 - t1 );
        double d2 = std_tick_to_micro_f64 ( t3 - t2 );
        std_log_info_m ( "[arbitrary random size alloc] malloc:" std_fmt_f64_m " std:" std_fmt_f64_m, d1, d2 );
    }
    allocs2[0] = std_virtual_heap_alloc ( 8, 8 );
    {
        size_t alloc_sizes[] = {
            8,
            16,
            4,
            16,
            2,
            4,
            8,
            16,
            64,
            4,
            4,
            8,
            4,
        }; // 13
        std_tick_t t1 = std_tick_now();

        for ( size_t j = 0; j < 100; ++j ) {
            for ( size_t i = 0; i < 13; ++i ) {
                allocs1[i] = _aligned_malloc ( alloc_sizes[i], 8 );
                ( ( volatile int* ) allocs1[i] ) [0] = ( ( int* ) allocs1[i] ) [0];
            }
        }

        std_tick_t t2 = std_tick_now();

        for ( size_t j = 0; j < 100; ++j ) {
            for ( size_t i = 0; i < 13; ++i ) {
                allocs2[i] = std_virtual_heap_alloc ( alloc_sizes[i], 8 );
                ( ( volatile int* ) allocs1[i] ) [0] = ( ( int* ) allocs1[i] ) [0];
            }
        }

        std_tick_t t3 = std_tick_now();

        double d1 = std_tick_to_micro_f64 ( t2 - t1 );
        double d2 = std_tick_to_micro_f64 ( t3 - t2 );
        std_log_info_m ( "[arbitrary small size alloc] malloc:" std_fmt_f64_m " std:" std_fmt_f64_m, d1, d2 );
    }

    {
        std_tick_t t1 = std_tick_now();

        for ( size_t i = 0; i < 13; ++i ) {
            _aligned_free ( allocs1[i] );
        }

        std_tick_t t2 = std_tick_now();

        for ( size_t i = 0; i < 13; ++i ) {
            std_virtual_heap_free ( allocs2[i].handle );
        }

        std_tick_t t3 = std_tick_now();

        double d1 = std_tick_to_micro_f64 ( t2 - t1 );
        double d2 = std_tick_to_micro_f64 ( t3 - t2 );
        std_log_info_m ( "[arbitrary small size free] malloc:" std_fmt_f64_m " std:" std_fmt_f64_m, d1, d2 );
    }
#endif
    {
        //std_alloc_t allocs[1000 * 100];
        void* ptrs[1000 * 100];
        size_t alloc_count = 0;
        float p = 0.01;

        std_tick_t t1 = std_tick_now();

        for ( size_t i = 0; i < 1000 * 100; ++i ) {
            uint64_t random = xorshift64star();
            float r = xorshift_to_f32 ( random );

            if ( alloc_count && r > p ) {
                uint64_t idx = random % ( alloc_count );
                _aligned_free ( ptrs[idx] );
                ptrs[idx] = ptrs[--alloc_count];
            } else {
                ptrs[alloc_count++] = _aligned_malloc ( random % 1023 + 1, 16 );
            }
        }

        std_tick_t t2 = std_tick_now();

        for ( size_t i = 0; i < alloc_count; ++i ) {
            _aligned_free ( ptrs[i] );
        }

        alloc_count = 0;

        std_tick_t t3 = std_tick_now();

        for ( size_t i = 0; i < 1000 * 100; ++i ) {
            uint64_t random = xorshift64star();
            float r = xorshift_to_f32 ( random );

            if ( alloc_count && r > p ) {
                uint64_t idx = random % ( alloc_count );
                std_virtual_heap_free ( ptrs[idx] );
                ptrs[idx] = ptrs[--alloc_count];
            } else {
                ptrs[alloc_count++] = std_virtual_heap_alloc_m ( random % 1023 + 1, 16 );
            }
        }

        std_tick_t t4 = std_tick_now();

        for ( size_t i = 0; i < alloc_count; ++i ) {
            std_virtual_heap_free ( ptrs[i] );
        }

        double d1 = std_tick_to_micro_f64 ( t2 - t1 );
        double d2 = std_tick_to_micro_f64 ( t4 - t3 );
        std_log_info_m ( "[mixed alloc free] malloc:" std_fmt_f64_m " std:" std_fmt_f64_m, d1, d2 );
    }
#endif
}
#endif

static void test_platform ( void ) {
    std_log_info_m ( "testing std_platform..." );
    std_platform_logical_core_info_t logical_cores_info[32];
    std_platform_physical_core_info_t physical_cores_info[32];
    std_platform_cache_info_t caches_info[32];
    std_platform_memory_info_t memory_info = std_platform_memory_info();
    size_t physical_cores_count = std_platform_physical_cores_info ( physical_cores_info, 32 );
    size_t logical_cores_count = std_platform_logical_cores_info ( logical_cores_info, 32 );
    size_t caches_count = std_platform_caches_info ( caches_info, 32 );

    {
        char ram_size[32];
        char swap_size[32];
        char vpage_size[32];
        std_size_to_str_approx ( ram_size, 32, memory_info.total_ram_size );
        std_size_to_str_approx ( swap_size, 32, memory_info.total_swap_size );
        std_size_to_str_approx ( vpage_size, 32, memory_info.virtual_page_size );
        std_log_info_m ( "RAM: " std_fmt_str_m ", Swap: " std_fmt_str_m ", OS virtual page size: " std_fmt_str_m, ram_size, swap_size, vpage_size );
    }

    for ( size_t i = 0; i < physical_cores_count; ++i ) {
        char mask[65];
        std_u64_to_bin ( physical_cores_info[i].logical_cores_mask, mask );
        std_log_info_m ( "Physical core " std_fmt_size_m": logical cores mask " std_fmt_str_m, i, mask + 64 - logical_cores_count );
    }

    for ( size_t i = 0; i < logical_cores_count; ++i ) {
        std_log_info_m ( "Logical core " std_fmt_size_m": physical core " std_fmt_size_m, i, logical_cores_info[i].physical_core_idx );
    }
}

static void  test_allocator ( void ) {
    std_log_info_m ( "testing std_allocator..." );
    {
        size_t virtual_page_size = std_virtual_page_size();
        std_assert_m ( virtual_page_size > 0 );
        std_assert_m ( virtual_page_size == std_platform_memory_info().virtual_page_size );

        void* a = std_virtual_reserve ( virtual_page_size );
        std_verify_m ( std_virtual_map ( a, a + virtual_page_size ) );
        volatile int* d = ( volatile int* ) a;
        int t = 0;

        for ( size_t i = 0; i < virtual_page_size / sizeof ( int ); ++i ) {
            d[i] = ( int ) i;
            t += d[i];
        }

        std_verify_m ( std_virtual_unmap ( a, a + virtual_page_size ) );
        std_verify_m ( std_virtual_free ( a, a + virtual_page_size ) );
    }
    {
        uint32_t* a = std_virtual_heap_alloc_array_m ( uint32_t, 10 );
        uint32_t* b = std_virtual_heap_alloc_array_m ( uint32_t, 10 );
        uint32_t* c = ( uint32_t* ) ( std_virtual_heap_alloc_m ( sizeof ( uint32_t ) * 10, std_alignof_m ( uint32_t ) ) );

        for ( size_t i = 0; i < 10; ++i ) {
            a[i] = ( uint32_t ) i;
        }

        std_mem_copy ( b, a, sizeof ( uint32_t ) * 10 );
        std_mem_copy_array_m ( c, b, 10 );

        std_verify_m ( std_mem_cmp ( a, c, sizeof ( uint32_t ) * 10 ) );
        std_verify_m ( std_mem_cmp_array_m ( a, c, 10 ) );

        std_verify_m ( std_virtual_heap_free ( a ) );
        std_verify_m ( std_virtual_heap_free ( b ) );
        std_verify_m ( std_virtual_heap_free ( c ) );
    }
    {
        void* buffer = std_virtual_heap_alloc_m ( 1024 * 32, 16 );
        std_stack_t stack = std_stack ( buffer, 1024 * 32 );
        void* a = std_stack_alloc_align ( &stack, 1024 * 16, 16 );
        void* b = std_stack_alloc_align ( &stack, 1024 * 8, 16 );
        void* c = std_stack_alloc_array_m ( &stack, uint32_t, 1024 );
        void* d = std_stack_alloc_array_m ( &stack, uint32_t, 1024 );
        std_virtual_heap_free ( buffer );
    }
    #if 0
    {
        size_t tagged_page_size = std_tagged_page_size();
        std_buffer_t b1 = std_tagged_alloc ( tagged_page_size, 5 );
        std_buffer_t b2 = std_tagged_alloc ( tagged_page_size * 2, 5 );
        std_buffer_t b3 = std_tagged_alloc ( tagged_page_size * 10, 7 );
        std_assert_m ( b1.base && b2.base && b3.base );
        std_tagged_free ( 5 );
        std_tagged_free ( 7 );
    }
    #endif
    for ( uint32_t i = 0; i < 100; ++i ) {
        void* alloc = std_virtual_heap_alloc_m ( 256, 8 );
        void* alloc2 = std_virtual_heap_alloc_m ( 2435, 8 );
        std_virtual_heap_free ( alloc2 );
        std_virtual_heap_free ( alloc );
    }

    {
        char reserved_size[16];
        char mapped_size[16];
        char used_heap_size[16];
        char total_heap_size[16];
        std_allocator_info_t info;
        std_allocator_info ( &info );
        std_size_to_str_approx ( reserved_size, 16, info.reserved_size );
        std_size_to_str_approx ( mapped_size, 16, info.mapped_size );
        std_size_to_str_approx ( used_heap_size, 16, info.used_heap_size );
        std_size_to_str_approx ( total_heap_size, 16, info.total_heap_size );
        std_log_info_m ( "Reserved: " std_fmt_str_m, reserved_size );
        std_log_info_m ( "Mapped: " std_fmt_str_m, mapped_size );
        std_log_info_m ( "Used heap: " std_fmt_str_m, used_heap_size );
        std_log_info_m ( "Total heap: " std_fmt_str_m, total_heap_size );
    }

    std_log_info_m ( "std_allocator test complete." );
}

static void test_process ( void ) {
    std_log_info_m ( "testing std_process..." );

    std_process_info_t info;
    std_process_info ( &info, std_process_this() );

    {
        std_process_io_t io = std_process_get_io ( std_process_this() );
#if defined(std_platform_win32_m)
        std_assert_m ( info.io.stdin_handle == ( uint64_t ) ( GetStdHandle ( STD_INPUT_HANDLE ) ) );
        std_assert_m ( info.io.stdout_handle == ( uint64_t ) ( GetStdHandle ( STD_OUTPUT_HANDLE ) ) );
        std_assert_m ( info.io.stderr_handle == ( uint64_t ) ( GetStdHandle ( STD_ERROR_HANDLE ) ) );
#elif defined(std_platform_linux_m)
        std_assert_m ( info.io.stdin_handle == ( uint64_t ) STDIN_FILENO );
        std_assert_m ( info.io.stdout_handle == ( uint64_t ) STDOUT_FILENO );
        std_assert_m ( info.io.stderr_handle == ( uint64_t ) STDERR_FILENO );
#endif
    }

    // create named pipe
    std_process_pipe_params_t pipe_params = {
        .name = "std_test_pipe",
        .flags = std_process_pipe_flags_write_m | std_process_pipe_flags_blocking_m,
        .write_capacity = 1024,
        .read_capacity = 1024,
    };
    std_pipe_h pipe = std_process_pipe_create ( &pipe_params );
    std_assert_m ( pipe != std_process_null_handle_m );

    // create child process
    const char* process_arg = CHILD_PROCESS_MAGIC_NUMBER;
    const char* process_path = info.executable_path;
    std_process_h process = std_process ( process_path, "std_test", &process_arg, 1, std_process_type_default_m, std_process_io_capture_m );

    // wait for child to connect to pipe and write to it
    std_process_pipe_wait_for_connection ( pipe );
    char* write_buffer = "pipe_write_data";
    bool write_result = std_process_pipe_write ( NULL, pipe, write_buffer, std_str_len ( write_buffer ) + 1 );
    std_assert_m ( write_result );

    std_process_pipe_destroy ( pipe );
    pipe = std_process_pipe_connect ( pipe_params.name, std_process_pipe_flags_read_m | std_process_pipe_flags_blocking_m );

    // read echo from pipe
    {
        char buffer[64];
        size_t data_size = 0;
        bool read_result = std_process_pipe_read ( &data_size, buffer, sizeof ( buffer ), pipe );
        std_assert_m ( read_result );
        bool match = std_str_cmp ( buffer, write_buffer ) == 0;
        std_assert_m ( match );
    }

    std_process_pipe_destroy ( pipe );

    std_process_io_t io = std_process_get_io ( process );
    // write to child stdin
    {
        char buffer[64];
        std_str_copy ( buffer, sizeof ( buffer ), PARENT_PROCESS_MESSAGE );
        std_process_io_write ( io.stdin_handle, NULL, buffer, sizeof ( PARENT_PROCESS_MESSAGE ) );
    }

    // read echo from child stdout
    {
        char buffer[64];
        std_process_io_read ( buffer, NULL, sizeof ( PARENT_PROCESS_MESSAGE ), io.stdout_handle ) ;
        bool match = std_str_cmp ( buffer, PARENT_PROCESS_MESSAGE ) == 0;
        std_assert_m ( match );
    }

    // read msg from child stdout
    {
        char buffer[64];
        std_process_io_read ( buffer, NULL, sizeof ( CHILD_PROCESS_OUTPUT ), io.stdout_handle );
        bool match = std_str_cmp ( buffer, CHILD_PROCESS_OUTPUT ) == 0;
        std_assert_m ( match );
    }

    std_process_wait_for ( process );

    std_log_info_m ( "std_process test complete." );
}

static void test_process_child ( void ) {
    std_pipe_h pipe = std_process_pipe_connect ( "std_test_pipe", std_process_pipe_flags_read_m | std_process_pipe_flags_blocking_m );

    char buffer[64];
    size_t data_size = 0;

    std_process_pipe_read ( &data_size, buffer, sizeof ( buffer ), pipe );
    std_process_pipe_destroy ( pipe );
    pipe = std_process_pipe_connect ( "std_test_pipe", std_process_pipe_flags_write_m | std_process_pipe_flags_blocking_m );
    std_process_pipe_write ( NULL, pipe, buffer, data_size  );
    std_process_pipe_destroy ( pipe );

    std_process_io_t io = std_process_get_io ( std_process_this() );

    std_process_io_read ( buffer, &data_size, sizeof ( PARENT_PROCESS_MESSAGE ), io.stdin_handle );
    std_process_io_write ( io.stdout_handle, NULL, buffer, data_size );
    std_process_io_write ( io.stdout_handle, NULL, CHILD_PROCESS_OUTPUT, sizeof ( CHILD_PROCESS_OUTPUT ) );
}

static void test_thread_body ( void* arg ) {
    uint32_t* flag = ( uint32_t* ) arg;
    std_thread_info_t info;
    std_thread_info ( &info, std_thread_this() );
    uint32_t uid = std_thread_uid ( std_thread_this() );
    uint32_t idx = std_thread_index ( std_thread_this() );
    char stack_size_str[32];
    std_size_to_str_approx ( stack_size_str, 32, info.stack_size );
    std_log_info_m ( "Child thread uid: " std_fmt_u32_m ", index: " std_fmt_u32_m ", stack size: " std_fmt_u64_m " (" std_fmt_str_m ")", uid, idx, info.stack_size, stack_size_str );
    *flag = true;
}

static void test_thread ( void ) {
    std_log_info_m ( "testing std_thread..." );

    {
        std_thread_info_t info;
        std_thread_info ( &info, std_thread_this() );
        uint32_t uid = std_thread_uid ( std_thread_this() );
        uint32_t idx = std_thread_index ( std_thread_this() );
        char stack_size_str[32];
        std_size_to_str_approx ( stack_size_str, 32, info.stack_size );
        std_log_info_m ( "Main thread uid: " std_fmt_u32_m ", index: " std_fmt_u32_m ", stack size: " std_fmt_u64_m " (" std_fmt_str_m ")", uid, idx, info.stack_size, stack_size_str );
    }

    {
        uint32_t flag = false;
        std_thread_h thread = std_thread ( test_thread_body, &flag, "test_thread", std_thread_core_mask_any_m );
        bool result;
        result = std_thread_join ( thread );
        std_assert_m ( result );
        std_assert_m ( flag );
        result = std_thread_alive ( thread );
        std_assert_m ( !result );
    }

    {
        uint32_t flag = false;
        std_thread_h thread = std_thread ( test_thread_body, &flag, "test_thread", std_thread_core_mask_any_m );
        bool result;
        result = std_thread_join ( thread );
        std_assert_m ( result );
        std_assert_m ( flag );
        result = std_thread_alive ( thread );
        std_assert_m ( !result );
    }

    std_log_info_m ( "std_thread test complete." );
}

static void test_module ( void ) {
    std_log_info_m ( "testing std_module..." );
    std_log_info_m ( "std_module_test complete. [TODO: expand this test]" );
}

typedef struct {
    std_queue_shared_t* queue;
    size_t n;
} test_queue_spsc_thread_args_t;

static void test_queue_spsc_thread ( void* arg ) {
    std_auto_m args = ( test_queue_spsc_thread_args_t* ) arg;

    for ( size_t i = 0; i < args->n; ++i ) {
        int j = ( int ) i;
        std_queue_spsc_push ( args->queue, &j, sizeof ( j ) );
    }
}

typedef struct {
    uint32_t a;
    uint32_t b;
    uint64_t c;
} test_queue_item_t;

typedef struct {
    std_queue_shared_t* queue;
    void* base;
    size_t n;
} test_queue_thread_args_t;

static int test_queue_item_compare ( const void* a, const void* b ) {
    std_auto_m i = ( test_queue_item_t* ) a;
    std_auto_m j = ( test_queue_item_t* ) b;
    return ( int ) ( i->c - j->c );
}

static int u64_compare ( const void* a, const void* b ) {
    std_auto_m i = ( uint64_t* ) a;
    std_auto_m j = ( uint64_t* ) b;
    return ( int ) ( i - j );
}

static int u32_compare ( const void* a, const void* b ) {
    std_auto_m i = ( uint32_t* ) a;
    std_auto_m j = ( uint32_t* ) b;
    return ( int ) ( i - j );
}

static void test_queue_spmc_thread ( void* arg ) {
    std_auto_m args = ( test_queue_thread_args_t* ) arg;
    std_auto_m items = ( test_queue_item_t* ) args->base;

    for ( size_t i = 0; i < args->n; ++i ) {
        test_queue_item_t* item = &items[i];
        size_t size = std_queue_spmc_pop_move ( args->queue, item, sizeof ( test_queue_item_t ) );

        while ( size == 0 ) {
            //std_thread_this_yield();
            size = std_queue_spmc_pop_move ( args->queue, item, sizeof ( test_queue_item_t ) );
        }

        std_assert_m ( size == sizeof ( test_queue_item_t ) );
    }
}

static void test_queue_mpsc_thread ( void* arg ) {
    std_auto_m args = ( test_queue_thread_args_t* ) arg;
    std_auto_m items = ( test_queue_item_t* ) args->base;

    for ( size_t i = 0; i < args->n; ++i ) {
        test_queue_item_t* item = &items[i];

        while ( !std_queue_mpsc_push ( args->queue, item, sizeof ( test_queue_item_t ) ) ) {
            //std_thread_this_yield();
        }
    }
}

static void test_queue_mpmc_p_thread ( void* arg ) {
    std_auto_m args = ( test_queue_thread_args_t* ) arg;
    std_auto_m items = ( test_queue_item_t* ) args->base;

    for ( size_t i = 0; i < args->n; ++i ) {
        test_queue_item_t* item = &items[i];

        while ( !std_queue_mpmc_push_m ( args->queue, item ) ) {
            //std_thread_this_yield();
        }
    }
}

static void test_queue_mpmc_c_thread ( void* arg ) {
    std_auto_m args = ( test_queue_thread_args_t* ) arg;
    std_auto_m items = ( test_queue_item_t* ) args->base;

    for ( size_t i = 0; i < args->n; ++i ) {
        test_queue_item_t* item = &items[i];
        size_t size = std_queue_mpmc_pop_move ( args->queue, item, sizeof ( test_queue_item_t ) );

        while ( size == 0 ) {
            //std_thread_this_yield();
            size = std_queue_mpmc_pop_move ( args->queue, item, sizeof ( test_queue_item_t ) );
        }

        std_assert_m ( size == sizeof ( test_queue_item_t ) );
    }
}

static void test_queue_mpmc_u64_p_thread ( void* arg ) {
    std_auto_m args = ( test_queue_thread_args_t* ) arg;
    std_auto_m items = ( uint64_t* ) args->base;

    for ( size_t i = 0; i < args->n; ++i ) {
        uint64_t* item = &items[i];

        while ( !std_queue_mpmc_push_64 ( args->queue, item ) ) {
            //std_thread_this_yield();
        }
    }
}

static void test_queue_mpmc_u64_c_thread ( void* arg ) {
    std_auto_m args = ( test_queue_thread_args_t* ) arg;
    std_auto_m items = ( uint64_t* ) args->base;

    for ( size_t i = 0; i < args->n; ++i ) {
        uint64_t* item = &items[i];

        while ( !std_queue_mpmc_pop_move_64 ( args->queue, item ) ) {
            //std_thread_this_yield();
        }
    }
}

static void test_queue_mpmc_u32_p_thread ( void* arg ) {
    std_auto_m args = ( test_queue_thread_args_t* ) arg;
    std_auto_m items = ( uint32_t* ) args->base;

    for ( size_t i = 0; i < args->n; ++i ) {
        uint32_t* item = &items[i];

        while ( !std_queue_mpmc_push_32 ( args->queue, item ) ) {
            //std_thread_this_yield();
        }
    }
}

static void test_queue_mpmc_u32_c_thread ( void* arg ) {
    std_auto_m args = ( test_queue_thread_args_t* ) arg;
    std_auto_m items = ( uint32_t* ) args->base;

    for ( size_t i = 0; i < args->n; ++i ) {
        uint32_t* item = &items[i];

        while ( !std_queue_mpmc_pop_move_32 ( args->queue, item ) ) {
            //std_thread_this_yield();
        }
    }
}

static void test_queue ( void ) {
    // Local queue
    {
        std_log_info_m ( "testing std_queue..." );
        size_t size = 16;
        std_queue_local_t queue = std_queue_local_create ( size );
        //std_assert_m ( std_queue_local_size ( &queue ) == size );
        std_assert_m ( std_queue_local_used_size ( &queue ) == 0 );
        int i = 5;

        std_queue_local_push ( &queue, &i, sizeof ( i ) );
        //std_assert_m ( std_queue_local_size ( &queue ) == size );
        std_assert_m ( std_queue_local_used_size ( &queue ) == sizeof ( i ) );

        struct {
            int i;
            int j;
        } s;
        s.j = 3;
        std_queue_local_pop_move ( &queue, &s.i, sizeof ( i ) );
        std_assert_m ( s.i == i );
        std_assert_m ( s.j == 3 );
        //std_assert_m ( std_queue_local_size ( &queue ) == size );
        std_assert_m ( std_queue_local_used_size ( &queue ) == 0 );

        struct {
            uint64_t u64;
            uint32_t u32;
        } t;
        t.u64 = 1;
        t.u32 = 2;

        std_queue_local_push ( &queue, &t, 12 );
        //std_assert_m ( std_queue_local_size ( &queue ) == size );
        std_assert_m ( std_queue_local_used_size ( &queue ) == 12 );

        std_queue_local_pop_discard ( &queue, 12 );
        //std_assert_m ( std_queue_local_size ( &queue ) == size );
        std_assert_m ( std_queue_local_used_size ( &queue ) == 0 );

        uint64_t j = 33;
        std_queue_local_push ( &queue, &j, sizeof ( j ) );
        //std_assert_m ( std_queue_local_size ( &queue ) == size );
        std_assert_m ( std_queue_local_used_size ( &queue ) == sizeof ( j ) );

        struct {
            uint64_t j;
            uint64_t k;
        } r;
        r.k = 9;
        std_queue_local_pop_move ( &queue, &r.j, sizeof ( j ) );
        std_assert_m ( r.j == j );
        //std_assert_m ( std_queue_local_size ( &queue ) == size );
        std_assert_m ( std_queue_local_used_size ( &queue ) == 0 );

        std_queue_local_destroy ( &queue );
    }

    // Shared SPSC queue
    {
        size_t size = 1024ull * 4 * 1024 * 16;
        std_queue_shared_t queue = std_queue_shared_create ( size );

        test_queue_spsc_thread_args_t args;
        args.queue = &queue;
        args.n = size / 4;
        std_thread_h thread = std_thread ( test_queue_spsc_thread, &args, "spsc producer", std_thread_core_mask_any_m );

        int j = -1;

        for ( size_t i = 0; i < args.n; ++i ) {
            while ( std_queue_shared_used_size ( &queue ) == 0 ) {
                std_thread_this_yield();
            }

            int k;
            std_queue_spsc_pop_move ( &queue, &k, sizeof ( k ) );
            std_assert_m ( k == j + 1 );
            j = k;
        }

        bool result;
        result = std_thread_join ( thread );
        std_assert_m ( result );

        std_queue_shared_destroy ( &queue );
    }

    // Shared SPMC and MPSC queues
    {
        size_t n = 1024 * 1000 * 1;
#define THREAD_COUNT 8
        std_assert_m ( n % THREAD_COUNT == 0 );
        size_t per_thread_n = n / THREAD_COUNT;
        void* threads_memory = std_virtual_heap_alloc_m ( n * sizeof ( test_queue_item_t ), 16 );

        // SPMC
        {
            size_t queue_size = std_pow2_round_up ( n * sizeof ( test_queue_item_t ) + n * sizeof ( uint64_t ) );
            std_queue_shared_t queue = std_queue_shared_create ( queue_size );

            test_queue_item_t item;

            test_queue_thread_args_t args[THREAD_COUNT];
            std_thread_h threads[THREAD_COUNT];

            for ( size_t i = 0 ; i < THREAD_COUNT; ++i ) {
                args[i].queue = &queue;
                args[i].n = per_thread_n;
                size_t size = sizeof ( test_queue_item_t ) * per_thread_n;
                args[i].base = threads_memory + i * size;

                threads[i] = std_thread ( test_queue_spmc_thread, &args[i], "spmc consumer", std_thread_core_mask_any_m );
            }

            for ( size_t i = 0; i < n; ++i ) {
                item.c = i;
                std_queue_spmc_push ( &queue, &item, sizeof ( item ) );
            }

            for ( size_t i = 0; i < THREAD_COUNT; ++i ) {
                bool result = std_thread_join ( threads[i] );
                std_assert_m ( result );
            }
            
            std_queue_shared_destroy ( &queue );
        }

        // MPSC
        {
            size_t queue_size = std_pow2_round_up ( n * sizeof ( test_queue_item_t ) + n * sizeof ( uint64_t ) );
            std_queue_shared_t queue = std_queue_shared_create ( queue_size );
            test_queue_item_t* merge_items = std_virtual_heap_alloc_array_m ( test_queue_item_t, n );

            test_queue_thread_args_t args[THREAD_COUNT];
            std_thread_h threads[THREAD_COUNT];

            for ( size_t i = 0; i < THREAD_COUNT; ++i ) {
                args[i].queue = &queue;
                args[i].n = per_thread_n;
                size_t size = sizeof ( test_queue_item_t ) * per_thread_n;
                args[i].base = threads_memory + i * size;

                threads[i] = std_thread ( test_queue_mpsc_thread, &args[i], "mpsc producer", std_thread_core_mask_any_m );
            }

            for ( size_t i = 0; i < n; ++i ) {
                test_queue_item_t* item = &merge_items[i];
                size_t size = std_queue_mpsc_pop_move ( &queue, item, sizeof ( *item ) );

                while ( size == 0 ) {
                    size = std_queue_mpsc_pop_move ( &queue, item, sizeof ( *item ) );
                }

                std_assert_m ( size == sizeof ( *item ) );
            }

            for ( size_t i = 0; i < THREAD_COUNT; ++i ) {
                bool result = std_thread_join ( threads[i] );
                std_assert_m ( result );
            }

            qsort ( merge_items, n, sizeof ( test_queue_item_t ), test_queue_item_compare );

            for ( size_t i = 0; i < n; ++i ) {
                test_queue_item_t* item = &merge_items[i];
                std_assert_m ( item->c == i );
            }

            std_queue_shared_destroy ( &queue );
            std_virtual_heap_free ( merge_items );
        }

        std_virtual_heap_free ( threads_memory );
#undef THREAD_COUNT
    }

    // Shared MPMC queue
    {
        size_t n = 1024 * 1000;
#define PRODUCE_THREAD_COUNT 1
#define CONSUME_THREAD_COUNT 1
        std_assert_m ( n % PRODUCE_THREAD_COUNT == 0 );
        std_assert_m ( n % CONSUME_THREAD_COUNT == 0 );
        size_t per_p_thread_n = n / PRODUCE_THREAD_COUNT;
        size_t per_c_thread_n = n / CONSUME_THREAD_COUNT;

        // Generic size MPMC
        {
            size_t queue_size = std_pow2_round_up ( n * sizeof ( test_queue_item_t ) + n * sizeof ( uint64_t ) );
            test_queue_item_t* read_memory = std_virtual_heap_alloc_array_m ( test_queue_item_t, n );
            test_queue_item_t* write_memory = std_virtual_heap_alloc_array_m ( test_queue_item_t, n );

            std_queue_shared_t queue = std_queue_shared_create ( queue_size );

            for ( size_t i = 0; i < n; ++i ) {
                test_queue_item_t* item = &read_memory[i];
                item->c = i;
            }

            test_queue_thread_args_t p_args[PRODUCE_THREAD_COUNT];
            test_queue_thread_args_t c_args[CONSUME_THREAD_COUNT];

            for ( size_t i = 0; i < PRODUCE_THREAD_COUNT; ++i ) {
                p_args[i].queue = &queue;
                p_args[i].n = per_p_thread_n;
                p_args[i].base = read_memory + i * per_p_thread_n;
            }

            for ( size_t i = 0 ; i < CONSUME_THREAD_COUNT; ++i ) {
                c_args[i].queue = &queue;
                c_args[i].n = per_c_thread_n;
                c_args[i].base = write_memory + i * per_c_thread_n;
            }

            std_thread_h p_threads[PRODUCE_THREAD_COUNT];
            std_thread_h c_threads[CONSUME_THREAD_COUNT];

            for ( size_t i = 0; i < CONSUME_THREAD_COUNT; ++i ) {
                c_threads[i] = std_thread ( test_queue_mpmc_c_thread, &c_args[i], "mpmc c thread", std_thread_core_mask_any_m );
            }

            for ( size_t i = 0 ; i < PRODUCE_THREAD_COUNT; ++i ) {
                p_threads[i] = std_thread ( test_queue_mpmc_p_thread, &p_args[i], "mpmc p thread", std_thread_core_mask_any_m );
            }

            for ( size_t i = 0; i < PRODUCE_THREAD_COUNT; ++i ) {
                std_thread_join ( p_threads[i] );
            }

            for ( size_t i = 0; i < CONSUME_THREAD_COUNT; ++i ) {
                std_thread_join ( c_threads[i] );
            }

            qsort ( write_memory, n, sizeof ( test_queue_item_t ), test_queue_item_compare );

            for ( size_t i = 0; i < n; ++i ) {
                test_queue_item_t* item = &write_memory[i];
                std_assert_m ( item->c == i );
            }

            std_queue_shared_destroy ( &queue );
            std_virtual_heap_free ( read_memory );
            std_virtual_heap_free ( write_memory );
        }

        // u64 specialized MPMC
        {
            size_t queue_size = std_pow2_round_up ( n * sizeof ( uint64_t ) );
            uint64_t* read_memory = std_virtual_heap_alloc_array_m ( uint64_t, n );
            uint64_t* write_memory = std_virtual_heap_alloc_array_m ( uint64_t, n );

            std_queue_shared_t queue = std_queue_mpmc_64_create ( queue_size );

            for ( size_t i = 0; i < n; ++i ) {
                uint64_t* item = &read_memory[i];
                *item = i;
            }

            test_queue_thread_args_t p_args[PRODUCE_THREAD_COUNT];
            test_queue_thread_args_t c_args[CONSUME_THREAD_COUNT];

            for ( size_t i = 0; i < PRODUCE_THREAD_COUNT; ++i ) {
                p_args[i].queue = &queue;
                p_args[i].n = per_p_thread_n;
                p_args[i].base = read_memory + i * per_p_thread_n;
            }

            for ( size_t i = 0 ; i < CONSUME_THREAD_COUNT; ++i ) {
                c_args[i].queue = &queue;
                c_args[i].n = per_c_thread_n;
                c_args[i].base = write_memory + i * per_c_thread_n;
            }

            std_thread_h p_threads[PRODUCE_THREAD_COUNT];
            std_thread_h c_threads[CONSUME_THREAD_COUNT];

            for ( size_t i = 0; i < CONSUME_THREAD_COUNT; ++i ) {
                c_threads[i] = std_thread ( test_queue_mpmc_u64_c_thread, &c_args[i], "mpmc 64 c thread", std_thread_core_mask_any_m );
            }

            for ( size_t i = 0 ; i < PRODUCE_THREAD_COUNT; ++i ) {
                p_threads[i] = std_thread ( test_queue_mpmc_u64_p_thread, &p_args[i], "mpmc 64 p thread", std_thread_core_mask_any_m );
            }

            for ( size_t i = 0; i < PRODUCE_THREAD_COUNT; ++i ) {
                std_thread_join ( p_threads[i] );
            }

            for ( size_t i = 0; i < CONSUME_THREAD_COUNT; ++i ) {
                std_thread_join ( c_threads[i] );
            }

            qsort ( write_memory, n, sizeof ( uint64_t ), u64_compare );

            for ( size_t i = 0; i < n; ++i ) {
                uint64_t* item = &write_memory[i];
                std_assert_m ( *item == i );
            }

            std_queue_shared_destroy ( &queue );
            std_virtual_heap_free ( read_memory );
            std_virtual_heap_free ( write_memory );
        }

        // u32 specialized MPMC
        {
            size_t queue_size = std_pow2_round_up ( sizeof ( uint32_t ) * n );
            uint32_t* read_memory = std_virtual_heap_alloc_array_m ( uint32_t, n );
            uint32_t* write_memory = std_virtual_heap_alloc_array_m ( uint32_t, n );

            std_queue_shared_t queue = std_queue_mpmc_32_create ( queue_size );

            for ( size_t i = 0; i < n; ++i ) {
                uint32_t* item = &read_memory[i];
                *item = ( uint32_t ) i;
            }

            test_queue_thread_args_t p_args[PRODUCE_THREAD_COUNT];
            test_queue_thread_args_t c_args[CONSUME_THREAD_COUNT];

            for ( size_t i = 0; i < PRODUCE_THREAD_COUNT; ++i ) {
                p_args[i].queue = &queue;
                p_args[i].n = per_p_thread_n;
                p_args[i].base = read_memory + i * per_p_thread_n;
            }

            for ( size_t i = 0 ; i < CONSUME_THREAD_COUNT; ++i ) {
                c_args[i].queue = &queue;
                c_args[i].n = per_c_thread_n;
                c_args[i].base = write_memory + i * per_c_thread_n;
            }

            std_thread_h p_threads[PRODUCE_THREAD_COUNT];
            std_thread_h c_threads[CONSUME_THREAD_COUNT];

            for ( size_t i = 0; i < CONSUME_THREAD_COUNT; ++i ) {
                c_threads[i] = std_thread ( test_queue_mpmc_u32_c_thread, &c_args[i], "mpmc 32 c thread", std_thread_core_mask_any_m );
            }

            for ( size_t i = 0 ; i < PRODUCE_THREAD_COUNT; ++i ) {
                p_threads[i] = std_thread ( test_queue_mpmc_u32_p_thread, &p_args[i], "mpmc 32 p thread", std_thread_core_mask_any_m );
            }

            for ( size_t i = 0; i < PRODUCE_THREAD_COUNT; ++i ) {
                std_thread_join ( p_threads[i] );
            }

            for ( size_t i = 0; i < CONSUME_THREAD_COUNT; ++i ) {
                std_thread_join ( c_threads[i] );
            }

            qsort ( write_memory, n, sizeof ( uint32_t ), u32_compare );

            for ( size_t i = 0; i < n; ++i ) {
                uint32_t* item = &write_memory[i];
                std_assert_m ( *item == i );
            }

            std_queue_shared_destroy ( &queue );
            std_virtual_heap_free ( read_memory );
            std_virtual_heap_free ( write_memory );
        }

#undef PRODUCE_THREAD_COUNT
#undef CONSUME_THREAD_COUNT
    }
    std_log_info_m ( "std_queue test complete." );
}

static void test_map ( void ) {
    size_t n = 1024;

    // std_map
    #if 0
    {
        typedef struct {
            uint64_t a;
            uint64_t b;
        } test_map_payload_t;

        std_buffer_t keys_alloc = std_virtual_heap_alloc ( sizeof ( uint64_t ) * n * 2, 16 );
        std_buffer_t payloads_alloc = std_virtual_heap_alloc ( sizeof ( test_map_payload_t ) * n * 2, 16 );

        std_map_t map = std_map ( keys_alloc, payloads_alloc, sizeof ( uint64_t ), sizeof ( test_map_payload_t ),
            std_map_hasher_u64, NULL, std_map_cmp_u64, NULL );

        // Insert in inverted order so that later on when removing collisions we can test for linear probing on removal
        for ( size_t i = n; i > 0; --i ) {
            uint64_t key = i - 1;
            test_map_payload_t payload;
            payload.a = i - 1;
            payload.b = i - 1;
            std_map_insert ( &map, &key, &payload );

            void* lookup = std_map_lookup ( &map, &key );
            std_assert_m ( lookup != NULL );
            test_map_payload_t* lookup_payload = ( test_map_payload_t* ) lookup;
            std_assert_m ( lookup_payload->a == payload.a && lookup_payload->b == payload.b );

            const uint64_t* get_key = ( const uint64_t* ) std_map_get_key ( &map, lookup );
            std_assert_m ( *get_key == key );
        }

        std_assert_m ( map.pop == n );

        for ( size_t i = 0; i < n; ++i ) {
            uint64_t key = i;
            test_map_payload_t payload;
            payload.a = i;
            payload.b = i;

            void* lookup = std_map_lookup ( &map, &key );
            std_assert_m ( lookup != NULL );
            test_map_payload_t* lookup_payload = ( test_map_payload_t* ) lookup;
            std_assert_m ( lookup_payload->a == payload.a && lookup_payload->b == payload.b );

            const uint64_t* get_key = ( const uint64_t* ) std_map_get_key ( &map, lookup );
            std_assert_m ( *get_key == key );
        }

        for ( size_t i = 0; i < n; ++i ) {
            uint64_t key = i;

            void* lookup = std_map_lookup ( &map, &key );
            std_assert_m ( lookup != NULL );

            std_map_remove ( &map, lookup );

            lookup = std_map_lookup ( &map, &key );
            std_assert_m ( lookup == NULL );

            for ( size_t j = i + 1; j < n; ++j ) {
                key = j;
                test_map_payload_t payload;
                payload.a = j;
                payload.b = j;

                lookup = std_map_lookup ( &map, &key );
                std_assert_m ( lookup != NULL );
                test_map_payload_t* lookup_payload = ( test_map_payload_t* ) lookup;
                std_assert_m ( lookup_payload->a == payload.a && lookup_payload->b == payload.b );

                const uint64_t* get_key = ( const uint64_t* ) std_map_get_key ( &map, lookup );
                std_assert_m ( *get_key == key );
            }
        }

        std_virtual_heap_free ( keys_alloc.base );
        std_virtual_heap_free ( payloads_alloc.base );
    }
    #endif

    // std_hash_map
    {
        uint64_t* keys_alloc = std_virtual_heap_alloc_array_m ( uint64_t, n * 2 );
        uint64_t* payloads_alloc = std_virtual_heap_alloc_array_m ( uint64_t, n * 2 );

        std_hash_map_t map = std_hash_map ( keys_alloc, payloads_alloc, n * 2 );

        for ( size_t i = n; i > 0; --i ) {
            uint64_t key = std_hash_64_m ( i - 1 );
            uint64_t payload = i - 1;
            std_hash_map_insert ( &map, key, payload );

            void* lookup = std_hash_map_lookup ( &map, key );
            std_assert_m ( lookup != NULL );
            uint64_t* lookup_payload = ( uint64_t* ) lookup;
            std_assert_m ( *lookup_payload == payload );
        }

        std_assert_m ( map.count == n );

        for ( size_t i = 0; i < n; ++i ) {
            uint64_t key = std_hash_64_m ( i );
            uint64_t payload = i;

            void* lookup = std_hash_map_lookup ( &map, key );
            std_assert_m ( lookup != NULL );
            uint64_t* lookup_payload = ( uint64_t* ) lookup;
            std_assert_m ( *lookup_payload == payload );
        }

        for ( size_t i = 0; i < n; ++i ) {
            uint64_t key = std_hash_64_m ( i );

            void* lookup = std_hash_map_lookup ( &map, key );
            std_assert_m ( lookup != NULL );

            std_assert_m ( std_hash_map_remove_hash ( &map, key ) );

            lookup = std_hash_map_lookup ( &map, key );
            std_assert_m ( lookup == NULL );

            for ( size_t j = i + 1; j < n; ++j ) {
                key = std_hash_64_m ( j );
                uint64_t payload = j;

                lookup = std_hash_map_lookup ( &map, key );
                std_assert_m ( lookup != NULL );
                uint64_t* lookup_payload = ( uint64_t* ) lookup;
                std_assert_m ( *lookup_payload == payload );
            }
        }

        std_virtual_heap_free ( keys_alloc );
        std_virtual_heap_free ( payloads_alloc );
    }

    std_log_info_m ( "std_map test complete." );
}

static void test_time ( void ) {
    std_log_info_m ( "testing std_time..." );
    {
        std_calendar_time_t time;
        time.year = 1111;
        time.month = 12;
        time.day = 28;
        time.hour = 22;
        time.minute = 44;
        time.second = 55;
        time.millisecond = 666;
        std_timestamp_t timestamp = std_calendar_to_timestamp ( time );
        std_calendar_time_t time2 = std_timestamp_to_calendar ( timestamp );
        std_assert_m ( time.year == time2.year );
        std_assert_m ( time.month == time2.month );
        std_assert_m ( time.day == time2.day );
        std_assert_m ( time.hour == time2.hour );
        std_assert_m ( time.minute == time2.minute );
        std_assert_m ( time.second == time2.second );
        std_assert_m ( time.millisecond == time2.millisecond );
        //std_log_info_m ( "year: " std_fmt_u64_m, time2.year );
        //std_log_info_m ( "month: " std_fmt_u8_m, time2.month );
        //std_log_info_m ( "day: " std_fmt_u8_m, time2.day );
        //std_log_info_m ( "hour: " std_fmt_u8_m, time2.hour );
        //std_log_info_m ( "minute: " std_fmt_u8_m, time2.minute );
        //std_log_info_m ( "second: " std_fmt_u8_m, time2.second );
        //std_log_info_m ( "millisecond: " std_fmt_u16_m, time2.millisecond );
        char str[64];
        std_calendar_to_string ( time, str, 64 );
        bool result = std_str_cmp ( str, "1111/12/28 22:44:55.666" ) == 0;
        std_assert_m ( result );
    }
    {
        std_timestamp_t timestamp;// = std_timestamp_now_local();
        timestamp.count = 49847569;
        std_calendar_time_t time = std_timestamp_to_calendar ( timestamp );
        std_timestamp_t timestamp2 = std_calendar_to_timestamp ( time );
        std_assert_m ( timestamp.count == timestamp2.count );
    }
    {
        std_calendar_time_t local_time = std_calendar_time_now_local();
        char str[64];
        std_calendar_to_string ( local_time, str, 64 );
        std_log_info_m ( "Current local time: " std_fmt_str_m, str );
    }
    {
        char str[64];
        std_calendar_time_t local_time = std_calendar_time_now_local();
        std_timestamp_t timestamp = std_calendar_to_timestamp ( local_time );
        std_timestamp_to_string ( timestamp, str, 64 );
        std_log_info_m ( "Current local time: " std_fmt_str_m, str );
    }
    {
        char str[64];
        std_timestamp_t local_time = std_timestamp_now_local();
        std_timestamp_to_string ( local_time, str, 64 );
        std_log_info_m ( "Current local time: " std_fmt_str_m, str );
    }
    {
        char str[64];
        std_timestamp_t time = std_timestamp_now_utc();
        std_timestamp_to_string ( time, str, 64 );
        std_log_info_m ( "Current UTC time: " std_fmt_str_m, str );
    }
    std_log_info_m ( "std_time test complete." );
}

static void test_byte ( void ) {
    std_log_info_m ( "testing std_byte..." );
    {
        uint64_t u64 = 0;
        uint32_t idx;

        u64 = std_bit_set_64_m ( u64, 18 );
        idx = std_bit_scan_64 ( u64 );
        std_assert_m ( idx == 18 );
        idx = std_bit_scan_rev_64 ( u64 );
        std_assert_m ( idx == 63 - 18 );
    }
    {
        uint64_t bitset[8] = {0};
        bool found;
        uint64_t idx;

        //
        bitset[4] = std_bit_set_64_m ( bitset[4], 9 );

        found = std_bitset_scan ( &idx, bitset, 0, 8 );
        std_assert_m ( found );
        std_assert_m ( idx == 64 * 4 + 9 );

        found = std_bitset_scan_rev ( &idx, bitset, 64 * 8 - 1 );
        std_assert_m ( found );
        std_assert_m ( idx == 64 * 4 + 9 );

        //
        bitset[6] = std_bit_set_64_m ( bitset[6], 9 );
        bitset[2] = std_bit_set_64_m ( bitset[2], 9 );

        found = std_bitset_scan ( &idx, bitset, 0, 8 );
        std_assert_m ( found );
        std_assert_m ( idx == 64 * 2 + 9 );

        found = std_bitset_scan ( &idx, bitset, 64 * 2 + 30, 8 );
        std_assert_m ( found );
        std_assert_m ( idx == 64 * 4 + 9 );

        found = std_bitset_scan_rev ( &idx, bitset, 64 * 8 - 1 );
        std_assert_m ( found );
        std_assert_m ( idx == 64 * 6 + 9 );

        found = std_bitset_scan_rev ( &idx, bitset, 64 * 6 - 1 );
        std_assert_m ( found );
        std_assert_m ( idx == 64 * 4 + 9 );

        //
        found = std_bitset_scan ( &idx, bitset, 64 * 7, 8 );
        std_assert_m ( !found );

        found = std_bitset_scan_rev ( &idx, bitset, 32 );
        std_assert_m ( !found );
    }
    std_log_info_m ( "std_byte test complete." );
}

static void test_file ( void ) {
    std_log_info_m ( "testing std_file..." );

    std_process_info_t process_info;
    std_process_info ( &process_info, std_process_this() );

    {
        char path[256];
        std_str_copy ( path, 256, process_info.working_path );
        size_t len = std_path_pop ( path );
        size_t len2 = std_str_len ( path );
        std_assert_m ( len == len2 );
        std_log_info_m ( std_fmt_str_m, path );
        len = std_path_pop ( path );
        len2 = std_str_len ( path );
        std_assert_m ( len == len2 );
        std_log_info_m ( std_fmt_str_m, path );
        len = std_path_pop ( path );
        len2 = std_str_len ( path );
        std_assert_m ( len == len2 );
        std_log_info_m ( std_fmt_str_m, path );
        len = std_path_append ( path, 256, "neo/" );
        len2 = std_str_len ( path );
        std_assert_m ( len == len2 );
        std_log_info_m ( std_fmt_str_m, path );
        char path2[256];
        len = std_path_normalize ( path2, 256, path );
        std_log_info_m ( std_fmt_str_m, path2 );
    }
    {
        char path[256];
        {
            char path2[256];
            std_str_copy ( path2, 256, process_info.executable_path );
            std_path_normalize ( path, 256, path2 );
        }
        std_log_info_m ( std_fmt_str_m, path );
        {
            std_file_info_t info;
            std_file_path_info ( &info, path );
            std_calendar_time_t creation_time = std_timestamp_to_calendar ( info.creation_time );
            std_calendar_time_t last_write_time = std_timestamp_to_calendar ( info.last_write_time );
            std_calendar_time_t last_access_time = std_timestamp_to_calendar ( info.last_access_time );
            char time[64];
            std_calendar_to_string ( creation_time, time, 64 );
            std_log_info_m ( std_fmt_tab_m"Creation time: "std_fmt_tab_m std_fmt_tab_m std_fmt_str_m, time );
            std_calendar_to_string ( last_access_time, time, 64 );
            std_log_info_m ( std_fmt_tab_m"Last access time: "std_fmt_tab_m std_fmt_str_m, time );
            std_calendar_to_string ( last_write_time, time, 64 );
            std_log_info_m ( std_fmt_tab_m"Last write time: "std_fmt_tab_m std_fmt_str_m, time );
        }
        std_path_pop ( path );
        std_log_info_m ( std_fmt_str_m, path );
        {
            std_directory_info_t info;
            std_directory_info ( &info, path );
            std_calendar_time_t creation_time = std_timestamp_to_calendar ( info.creation_time );
            std_calendar_time_t last_write_time = std_timestamp_to_calendar ( info.last_write_time );
            std_calendar_time_t last_access_time = std_timestamp_to_calendar ( info.last_access_time );
            char time[64];
            std_calendar_to_string ( creation_time, time, 64 );
            std_log_info_m ( std_fmt_tab_m"Creation time: "std_fmt_tab_m std_fmt_tab_m std_fmt_str_m, time );
            std_calendar_to_string ( last_access_time, time, 64 );
            std_log_info_m ( std_fmt_tab_m"Last access time: "std_fmt_tab_m std_fmt_str_m, time );
            std_calendar_to_string ( last_write_time, time, 64 );
            std_log_info_m ( std_fmt_tab_m"Last write time: "std_fmt_tab_m std_fmt_str_m, time );
        }
    }

    {
        char path[256];
        std_str_copy ( path, 256, process_info.working_path );
        std_log_info_m ( std_fmt_str_m, path );
        {
            char subdirs_buffer[256][32];
            char* subdirs[32];

            for ( size_t i = 0; i < 32; ++i ) {
                subdirs[i] = subdirs_buffer[i];
            }

            size_t n = std_directory_subdirs ( subdirs, 32, 256, path );
            for ( size_t i = 0; i < n; ++i ) {
                std_log_info_m ( std_fmt_tab_m std_fmt_str_m std_fmt_str_m, subdirs[i], "/" );
            }
        }

        {
            char files_buffer[256][32];
            char* files[32];

            for ( size_t i = 0; i < 32; ++i ) {
                files[i] = files_buffer[i];
            }

            size_t n = std_directory_files ( files, 32, 256, path );
            for ( size_t i = 0; i < n; ++i ) {
                std_log_info_m ( std_fmt_tab_m std_fmt_str_m, files[i] );
            }
        }
    }

    std_log_info_m ( "std_file test complete" );
}

#if 0 && defined ( std_compiler_gcc_m )
static void test_array_fun ( std_array_type_m ( int )* int_array ) {
    int_array->data[int_array->count++] = 2;
}

static void test_array ( void ) {
    std_log_info_m ( "testing std_array..." );
    int a[3];
    std_auto_m static_int_array = std_static_array_m ( int, a );
    std_auto_m heap_int_array = std_heap_array_m ( int, 3 );
    static_int_array.data[static_int_array.count++] = 1;
    test_array_fun ( &static_int_array );
    test_array_fun ( &heap_int_array );
    std_assert_m ( static_int_array.data[0] == 1 );
    std_assert_m ( static_int_array.data[1] == 2 );
    std_assert_m ( heap_int_array.data[0] == 2 );

    typedef struct {
        char data[32];
    } string_t;

    std_auto_m string_array = std_array_m ( string_t );
    string_array = std_heap_array_m ( string_t, 32 );

    std_log_info_m ( "std_array test complete." );
}
#else
static void test_array ( void ) {
    std_log_info_m ( "testing std_array..." );
    int a[3];
    std_auto_m array = std_static_array_m ( int, a );
    std_array_push_m ( &array, 5 );
    std_assert_m ( array.data[0] == 5 );
    std_assert_m ( array.count == 1 );
    std_assert_m ( array.capacity == 3 );
    std_log_info_m ( "std_array test complete." );
}
#endif

void std_main ( void ) {
    //std_log_set_callback ( log_callback );

    std_process_info_t process_info;
    std_process_info ( &process_info, std_process_this() );

    if ( process_info.args_count == 1 ) {
        if ( std_mem_cmp ( process_info.args[0], CHILD_PROCESS_MAGIC_NUMBER, sizeof ( CHILD_PROCESS_MAGIC_NUMBER ) - 1 ) ) {
            test_process_child();
            return;
        }
    }

    const char* separator = "------------------------------------------";

#if 0
    uint64_t n = 1024*1024*4;

    uint64_t* vec_a = std_virtual_heap_alloc_array_m ( uint64_t, n );
    uint64_t* vec_b = std_virtual_heap_alloc_array_m ( uint64_t, n );
    uint64_t* vec_c = std_virtual_heap_alloc_array_m ( uint64_t, n );
    uint64_t* vec_d = std_virtual_heap_alloc_array_m ( uint64_t, n );
    uint64_t* vec_e = std_virtual_heap_alloc_array_m ( uint64_t, n );

    for ( uint64_t i = 0; i < n; ++i ) {
        vec_a[i] = i;
    }

    std_xorshift64_state_t rng = std_xorshift64_state();
    uint64_t tmp;
    std_sort_shuffle ( &rng, vec_a, sizeof ( uint64_t ), n, &tmp );

    std_tick_t t1 = std_tick_now();
    
    for ( uint64_t i = 0; i < n; ++ i ) {
        vec_b[i] = vec_c[i];
    }

    std_tick_t t2 = std_tick_now();
    
    std_tick_t t3 = std_tick_now();
    
    for ( uint64_t i = 0; i < n; ++ i ) {
        vec_d[vec_a[i]] = vec_e[i];
    }

    std_tick_t t4 = std_tick_now();

    double d1 = std_tick_to_milli_f64 ( t2 - t1 );
    double d2 = std_tick_to_milli_f64 ( t4 - t3 );
    std_log_info_m ( "linear:" std_fmt_f64_m " random:" std_fmt_f64_m, d1, d2 );
#endif

#if 1
    test_platform();
    std_log_info_m ( separator );
    test_allocator();
    std_log_info_m ( separator );
    test_process();
    std_log_info_m ( separator );
    test_thread();
    std_log_info_m ( separator );
    test_module();
    std_log_info_m ( separator );
    test_map();
    std_log_info_m ( separator );
    test_time();
    std_log_info_m ( separator );
    test_byte();
    std_log_info_m ( separator );
    test_array();
    std_log_info_m ( separator );
    test_file();
    std_log_info_m ( separator );
    test_queue();
#else
    bench_virtual_heap();
#endif

    std_log_info_m ( separator );
    std_log_info_m ( "STD_TEST COMPLETE!" );
}
