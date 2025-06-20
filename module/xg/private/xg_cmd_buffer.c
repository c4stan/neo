#include "xg_cmd_buffer.h"

#include <std_mutex.h>
#include <std_list.h>
#include <std_queue.h>
#include <std_hash.h>
#include <std_compiler.h>

// PRIVATE
typedef struct {
    size_t execution_barriers_count;
    size_t memory_barriers_count;
    size_t texture_memory_barriers_count;
    // xg_execution_barrier_t[]
    // xg_memory_barrier_t[]
    // xg_memory_barrier_texture_t[]
} xg_cmd_barrier_args_t;

int xg_vertex_stream_binding_cmp_f ( const void* _a, const void* _b ) {
    xg_vertex_stream_binding_t* a = ( xg_vertex_stream_binding_t* ) _a;
    xg_vertex_stream_binding_t* b = ( xg_vertex_stream_binding_t* ) _b;
    return a->stream_id > b->stream_id ? 1 : a->stream_id < b->stream_id ? -1 : 0;
}

static xg_cmd_buffer_state_t* xg_cmd_buffer_state;

static void xg_cmd_buffer_alloc_memory ( xg_cmd_buffer_t* cmd_buffer ) {
    cmd_buffer->cmd_headers_allocator = std_virtual_stack_create ( xg_cmd_buffer_cmd_buffer_size_m );
    cmd_buffer->cmd_args_allocator = std_virtual_stack_create ( xg_cmd_buffer_cmd_buffer_size_m );
}

static void xg_cmd_buffer_free_memory ( xg_cmd_buffer_t* cmd_buffer ) {
    std_virtual_stack_destroy ( &cmd_buffer->cmd_headers_allocator );
    std_virtual_stack_destroy ( &cmd_buffer->cmd_args_allocator );
}

void xg_cmd_buffer_load ( xg_cmd_buffer_state_t* state ) {
    xg_cmd_buffer_state = state;

    state->cmd_buffers_array = std_virtual_heap_alloc_array_m ( xg_cmd_buffer_t, xg_cmd_buffer_max_cmd_buffers_m );
    std_mem_zero_array_m ( state->cmd_buffers_array, xg_cmd_buffer_max_cmd_buffers_m );
    state->cmd_buffers_freelist = std_freelist_m ( state->cmd_buffers_array, xg_cmd_buffer_max_cmd_buffers_m );

    for ( size_t i = 0; i < xg_cmd_buffer_preallocated_cmd_buffers_m; ++i ) {
        xg_cmd_buffer_t* cmd_buffer = &state->cmd_buffers_array[i];
        xg_cmd_buffer_alloc_memory ( cmd_buffer );
    }

    if ( xg_cmd_buffer_cmd_buffer_size_m % std_virtual_page_size() != 0 ) {
        std_log_warn_m ( "Command buffer size is not a multiple of the system virtual page size, sub-optimal memory usage could result from this." );
    }

    std_mutex_init ( &state->cmd_buffers_mutex );
}

void xg_cmd_buffer_reload ( xg_cmd_buffer_state_t* state ) {
    xg_cmd_buffer_state = state;
}

void xg_cmd_buffer_unload ( void ) {
    for ( size_t i = 0; i < xg_cmd_buffer_max_cmd_buffers_m; ++i ) {
        xg_cmd_buffer_t* cmd_buffer = &xg_cmd_buffer_state->cmd_buffers_array[i];
        xg_cmd_buffer_free_memory ( cmd_buffer );
    }

    std_virtual_heap_free ( xg_cmd_buffer_state->cmd_buffers_array );
    std_mutex_deinit ( &xg_cmd_buffer_state->cmd_buffers_mutex );
}

xg_cmd_buffer_h xg_cmd_buffer_create ( xg_workload_h workload ) {
    std_mutex_lock ( &xg_cmd_buffer_state->cmd_buffers_mutex );

    xg_cmd_buffer_t* cmd_buffer = std_list_pop_m ( &xg_cmd_buffer_state->cmd_buffers_freelist );

    if ( cmd_buffer->cmd_headers_allocator.begin == NULL ) {
        std_log_info_m ( "Allocating additional command buffer, consider increasing xg_cmd_buffer_preallocated_cmd_buffers_m." );
        // Buffers are linearly added to the pool. Therefore if freeing is allowed it must also be linear and back-to-front.
        xg_cmd_buffer_alloc_memory ( cmd_buffer );
    }

    cmd_buffer->workload = workload;
    xg_cmd_buffer_h handle = ( xg_cmd_buffer_h ) ( cmd_buffer - xg_cmd_buffer_state->cmd_buffers_array );

    std_mutex_unlock ( &xg_cmd_buffer_state->cmd_buffers_mutex );

    return handle;
}

// TODO try moving to std_sort?
/*
    main source for current implementation:
        https://cboard.cprogramming.com/c-programming/158635-merge-sort-top-down-versus-bottom-up-3.html#post1174189

    various other sources and notes:
        https://www.1024cores.net/home/parallel-computing/radix-sort
        Radix sort:
            allocate a radix buffer
            iterate input array, split items into the radix bins
            recursively call on each bin
            propagate up the result by replacing the input array

        Counting sort:
            allocate a counter buffer
            iterate input array, count occurrences of each item
            fill output array

        Radix + counting sort:
            on some radix sort iterations it might be worth to do a counting sort pass instead of a radix
            radix sort does a single pass on the items and splits them into different bins
                1 input read, 1 output write
            counting sort used inside radix accumulated counters instead of splitting on the first pass,
            then does a pass on the counter to compute starting indices and finally does another pass on
            the input, this time to split the items into bins with the aid of the counter buffer
                2 input read, 1 count write, 1 count read, 1 output write

*/
void xg_cmd_buffer_sort ( xg_cmd_header_t* cmd_headers, xg_cmd_header_t* cmd_headers_temp, size_t cmd_header_cap, const xg_cmd_buffer_t** cmd_buffers, size_t cmd_buffer_count ) {
    size_t total_header_size = 0;

    for ( size_t i = 0; i < cmd_buffer_count; ++i ) {
        const xg_cmd_buffer_t* cmd_buffer = cmd_buffers[i];
        total_header_size += std_virtual_stack_used_size ( &cmd_buffer->cmd_headers_allocator );
    }

    size_t total_header_count = total_header_size / sizeof ( xg_cmd_header_t );
    std_assert_m ( total_header_count <= cmd_header_cap );

    // Stable u64 LSD radix sort using 8-bit bins
    void* buffer1 = cmd_headers_temp;
    void* buffer2 = cmd_headers;

    //
    // byte 0
    // source -> buffer1
    //
    uint64_t count_table[256] = {0};
    xg_cmd_header_t* input;
    xg_cmd_header_t* output;

    // Fill the count table
    for ( size_t i = 0; i < cmd_buffer_count; ++i ) {
        const xg_cmd_buffer_t* cmd_buffer = cmd_buffers[i];
        size_t header_size = std_virtual_stack_used_size ( &cmd_buffer->cmd_headers_allocator );
        const xg_cmd_header_t* cmd_header_begin = ( xg_cmd_header_t* ) ( cmd_buffer->cmd_headers_allocator.begin );
        const xg_cmd_header_t* cmd_header_end = ( xg_cmd_header_t* ) ( cmd_buffer->cmd_headers_allocator.begin + header_size );

        for ( const xg_cmd_header_t* header = cmd_header_begin; header < cmd_header_end; ++header ) {
            ++count_table[header->key & 255];
        }
    }

    // Accumulate count table
    for ( size_t i = 0, sum = 0; i < 256; ++i ) {
        uint64_t start = sum;
        sum += count_table[i];
        count_table[i] = start;
    }

    // Fill the output buffer
    output = buffer1;

    for ( size_t i = 0; i < cmd_buffer_count; ++i ) {
        const xg_cmd_buffer_t* cmd_buffer = cmd_buffers[i];
        size_t header_size = std_virtual_stack_used_size ( &cmd_buffer->cmd_headers_allocator );
        const xg_cmd_header_t* cmd_header_begin = ( xg_cmd_header_t* ) ( cmd_buffer->cmd_headers_allocator.begin );
        const xg_cmd_header_t* cmd_header_end = ( xg_cmd_header_t* ) ( cmd_buffer->cmd_headers_allocator.begin + header_size );

        for ( const xg_cmd_header_t* header = cmd_header_begin; header < cmd_header_end; ++header ) {
            uint64_t idx = count_table[header->key & 255]++;
            output[idx] = *header;
        }
    }

    //
    // byte 1
    // buffer1 -> buffer2
    //
    std_mem_zero_m ( count_table );
    input = buffer1;
    output = buffer2;

    for ( size_t i = 0; i < total_header_count; ++i ) {
        ++count_table[ ( input[i].key >> 8 ) & 255];
    }

    for ( size_t i = 0, sum = 0; i < 256; ++i ) {
        uint64_t start = sum;
        sum += count_table[i];
        count_table[i] = start;
    }

    for ( size_t i = 0; i < total_header_count; ++i ) {
        uint64_t idx = count_table[ ( input[i].key >> 8 ) & 255]++;
        output[idx] = input[i];
    }

    //
    // byte 2
    // bufer2 -> buffer1
    //
    std_mem_zero_m ( count_table );
    input = buffer2;
    output = buffer1;

    for ( size_t i = 0; i < total_header_count; ++i ) {
        ++count_table[ ( input[i].key >> 16 ) & 255];
    }

    for ( size_t i = 0, sum = 0; i < 256; ++i ) {
        uint64_t start = sum;
        sum += count_table[i];
        count_table[i] = start;
    }

    for ( size_t i = 0; i < total_header_count; ++i ) {
        uint64_t idx = count_table[ ( input[i].key >> 16 ) & 255]++;
        output[idx] = input[i];
    }

    //
    // byte 3
    // buffer1 -> buffer2
    //
    std_mem_zero_m ( count_table );
    input = buffer1;
    output = buffer2;

    for ( size_t i = 0; i < total_header_count; ++i ) {
        ++count_table[ ( input[i].key >> 24 ) & 255];
    }

    for ( size_t i = 0, sum = 0; i < 256; ++i ) {
        uint64_t start = sum;
        sum += count_table[i];
        count_table[i] = start;
    }

    for ( size_t i = 0; i < total_header_count; ++i ) {
        uint64_t idx = count_table[ ( input[i].key >> 24 ) & 255]++;
        output[idx] = input[i];
    }

    //
    // byte 4
    // buffer2 -> buffer1
    //
    std_mem_zero_m ( count_table );
    input = buffer2;
    output = buffer1;

    for ( size_t i = 0; i < total_header_count; ++i ) {
        ++count_table[ ( input[i].key >> 32 ) & 255];
    }

    for ( size_t i = 0, sum = 0; i < 256; ++i ) {
        uint64_t start = sum;
        sum += count_table[i];
        count_table[i] = start;
    }

    for ( size_t i = 0; i < total_header_count; ++i ) {
        uint64_t idx = count_table[ ( input[i].key >> 32 ) & 255]++;
        output[idx] = input[i];
    }

    //
    // byte 5
    // buffer1 -> buffer2
    //
    std_mem_zero_m ( count_table );
    input = buffer1;
    output = buffer2;

    for ( size_t i = 0; i < total_header_count; ++i ) {
        ++count_table[ ( input[i].key >> 40 ) & 255];
    }

    for ( size_t i = 0, sum = 0; i < 256; ++i ) {
        uint64_t start = sum;
        sum += count_table[i];
        count_table[i] = start;
    }

    for ( size_t i = 0; i < total_header_count; ++i ) {
        uint64_t idx = count_table[ ( input[i].key >> 40 ) & 255]++;
        output[idx] = input[i];
    }

    //
    // byte 6
    // buffer2 -> buffer1
    //
    std_mem_zero_m ( count_table );
    input = buffer2;
    output = buffer1;

    for ( size_t i = 0; i < total_header_count; ++i ) {
        ++count_table[ ( input[i].key >> 48 ) & 255];
    }

    for ( size_t i = 0, sum = 0; i < 256; ++i ) {
        uint64_t start = sum;
        sum += count_table[i];
        count_table[i] = start;
    }

    for ( size_t i = 0; i < total_header_count; ++i ) {
        uint64_t idx = count_table[ ( input[i].key >> 48 ) & 255]++;
        output[idx] = input[i];
    }

    //
    // byte 7
    // buffer1 -> output
    //
    std_mem_zero_m ( count_table );
    input = buffer1;
    output = buffer2;

    for ( size_t i = 0; i < total_header_count; ++i ) {
        ++count_table[ ( input[i].key >> 56 ) & 255];
    }

    for ( size_t i = 0, sum = 0; i < 256; ++i ) {
        uint64_t start = sum;
        sum += count_table[i];
        count_table[i] = start;
    }

    for ( size_t i = 0; i < total_header_count; ++i ) {
        uint64_t idx = count_table[ ( input[i].key >> 56 ) & 255]++;
        output[idx] = input[i];
    }

    //
    // the end
    // sorted result is in buffer2
}

void xg_cmd_buffer_destroy ( xg_cmd_buffer_h* cmd_buffer_handles, size_t count ) {
    std_mutex_lock ( &xg_cmd_buffer_state->cmd_buffers_mutex );

    for ( size_t i = 0; i < count; ++i ) {
        xg_cmd_buffer_h handle = cmd_buffer_handles[i];
        xg_cmd_buffer_t* cmd_buffer = &xg_cmd_buffer_state->cmd_buffers_array[handle];

        std_virtual_stack_clear ( &cmd_buffer->cmd_headers_allocator );
        std_virtual_stack_clear ( &cmd_buffer->cmd_args_allocator );

        std_list_push ( &xg_cmd_buffer_state->cmd_buffers_freelist, cmd_buffer );
    }

    std_mutex_unlock ( &xg_cmd_buffer_state->cmd_buffers_mutex );
}

// TODO make this return a const?
xg_cmd_buffer_t* xg_cmd_buffer_get ( xg_cmd_buffer_h cmd_buffer_handle ) {
    return &xg_cmd_buffer_state->cmd_buffers_array[cmd_buffer_handle];
}

// ======================================================================================= //
//                            C O M M A N D   R E C O R D I N G
// ======================================================================================= //

static void* xg_cmd_buffer_record_cmd ( xg_cmd_buffer_t* cmd_buffer, xg_cmd_type_e type, uint32_t tag, uint64_t key, size_t args_size ) {
    std_virtual_stack_align ( &cmd_buffer->cmd_headers_allocator, xg_cmd_buffer_cmd_alignment_m );
    std_virtual_stack_align ( &cmd_buffer->cmd_args_allocator, xg_cmd_buffer_cmd_alignment_m );

    xg_cmd_header_t* cmd_header = std_virtual_stack_alloc_m ( &cmd_buffer->cmd_headers_allocator, xg_cmd_header_t );
    void* cmd_args = NULL;

    if ( args_size ) {
        cmd_args = std_virtual_stack_alloc ( &cmd_buffer->cmd_args_allocator, args_size );
    }

    cmd_header->args = ( uint64_t ) cmd_args;
    cmd_header->type = ( uint8_t ) type;
    cmd_header->tag = ( uint8_t ) tag;
    cmd_header->key = key;

    return cmd_args;
}
#define xg_cmd_buffer_record_cmd_m( cmd_buffer, cmd_type, key, args_type ) \
    ( args_type* ) xg_cmd_buffer_record_cmd ( cmd_buffer, cmd_type, 0, key, sizeof ( args_type ) )

#define xg_cmd_buffer_record_cmd_noargs_m( cmd_buffer, cmd_type, key ) \
    xg_cmd_buffer_record_cmd ( cmd_buffer, cmd_type, 0, key, 0 )

#define xg_cmd_buffer_record_tag_cmd_m( cmd_buffer, cmd_type, tag, key, args_type ) \
    ( args_type* ) xg_cmd_buffer_record_cmd ( cmd_buffer, cmd_type, tag, key, sizeof ( args_type ) )

// Graphics pipeline

void xg_cmd_buffer_cmd_renderpass_begin ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, const xg_cmd_renderpass_params_t* params ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_graphics_renderpass_begin_m, key, xg_cmd_renderpass_params_t );

    *cmd_args = *params;
}

void xg_cmd_buffer_cmd_renderpass_end ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    xg_cmd_buffer_record_cmd_noargs_m ( cmd_buffer, xg_cmd_graphics_renderpass_end_m, key );
}

void xg_cmd_dynamic_viewport ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, const xg_viewport_state_t* viewport ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_dynamic_viewport_m, key, xg_viewport_state_t );
    *cmd_args = *viewport;
}

void xg_cmd_dynamic_scissor ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, const xg_scissor_state_t* scissor ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_dynamic_scissor_m, key, xg_scissor_state_t );
    *cmd_args = *scissor;
}

void xg_cmd_buffer_cmd_draw ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, const xg_cmd_draw_params_t* params ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_draw_m, key, xg_cmd_draw_params_t );

    *cmd_args = *params;
}

void xg_cmd_buffer_cmd_compute ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, const xg_cmd_compute_params_t* params ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_compute_m, key, xg_cmd_compute_params_t );

    *cmd_args = *params;
}

void xg_cmd_buffer_cmd_raytrace ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, const xg_cmd_raytrace_params_t* params ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_raytrace_m, key, xg_cmd_raytrace_params_t );

    *cmd_args = *params;
}

void xg_cmd_buffer_copy_buffer ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, const xg_buffer_copy_params_t* params ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_copy_buffer_m, key, xg_buffer_copy_params_t );

    *cmd_args = *params;
}

void xg_cmd_buffer_copy_texture ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, const xg_texture_copy_params_t* params ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_copy_texture_m, key, xg_texture_copy_params_t );

    *cmd_args = *params;
}

void xg_cmd_buffer_copy_buffer_to_texture ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, const xg_buffer_to_texture_copy_params_t* params ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_copy_buffer_to_texture_m, key, xg_buffer_to_texture_copy_params_t );

    *cmd_args = *params;
}

void xg_cmd_buffer_copy_texture_to_buffer ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, const xg_texture_to_buffer_copy_params_t* params ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_copy_texture_to_buffer_m, key, xg_texture_to_buffer_copy_params_t );

    *cmd_args = *params;
}

void xg_cmd_buffer_barrier_set ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, const xg_barrier_set_t* barrier_set ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_barrier_set_m, key, xg_cmd_barrier_set_t );

    //cmd_args->execution_barrier = barrier_set->execution_barrier;
    cmd_args->memory_barriers = ( uint32_t ) barrier_set->memory_barriers_count;
    cmd_args->buffer_memory_barriers = ( uint32_t ) barrier_set->buffer_memory_barriers_count;
    cmd_args->texture_memory_barriers = ( uint32_t ) barrier_set->texture_memory_barriers_count;

    if ( barrier_set->memory_barriers_count > 0 ) {
        std_virtual_stack_align ( &cmd_buffer->cmd_args_allocator, std_alignof_m ( xg_memory_barrier_t ) );
        xg_memory_barrier_t* barriers = std_virtual_stack_alloc_array_m ( &cmd_buffer->cmd_args_allocator, xg_memory_barrier_t, barrier_set->memory_barriers_count );
        std_mem_copy_array_m ( barriers, &barrier_set->memory_barriers, barrier_set->memory_barriers_count );
    }

    // TODO assert that all texture barriers have valid subresources (e.g. mip not set to xg_texture_all_mips_m)

    if ( barrier_set->buffer_memory_barriers_count > 0 ) {
        std_virtual_stack_align ( &cmd_buffer->cmd_args_allocator, std_alignof_m ( xg_buffer_memory_barrier_t ) );
        xg_buffer_memory_barrier_t* barriers = std_virtual_stack_alloc_array_m ( &cmd_buffer->cmd_args_allocator, xg_buffer_memory_barrier_t, barrier_set->buffer_memory_barriers_count );
        std_mem_copy_array_m ( barriers, barrier_set->buffer_memory_barriers, barrier_set->buffer_memory_barriers_count );
    }

    if ( barrier_set->texture_memory_barriers_count > 0 ) {
        std_virtual_stack_align ( &cmd_buffer->cmd_args_allocator, std_alignof_m ( xg_texture_memory_barrier_t ) );
        xg_texture_memory_barrier_t* barriers = std_virtual_stack_alloc_array_m ( &cmd_buffer->cmd_args_allocator, xg_texture_memory_barrier_t, barrier_set->texture_memory_barriers_count );
        
        for ( uint32_t i = 0; i < barrier_set->texture_memory_barriers_count; ++i ) {
            xg_texture_memory_barrier_t* barrier = &barrier_set->texture_memory_barriers[i];
            std_assert_m ( barrier->texture != xg_null_handle_m );
        }

        std_mem_copy_array_m ( barriers, barrier_set->texture_memory_barriers, barrier_set->texture_memory_barriers_count );
    }
}

void xg_cmd_bind_queue ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, const xg_cmd_bind_queue_params_t* params ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_tag_cmd_m ( cmd_buffer, xg_cmd_bind_queue_m, params->queue, key, xg_cmd_bind_queue_params_t );
    std_assert_m ( params->wait_count <= xg_cmd_bind_queue_max_wait_events_m );

    *cmd_args = *params;
}

void xg_cmd_buffer_clear_texture ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, xg_texture_h texture, xg_color_clear_t clear_color ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_texture_clear_m, key, xg_cmd_texture_clear_t );

    cmd_args->texture = texture;
    cmd_args->clear = clear_color;
}

void xg_cmd_buffer_clear_depth_stencil_texture ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, xg_texture_h texture, xg_depth_stencil_clear_t clear_value ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_texture_depth_stencil_clear_m, key, xg_cmd_texture_depth_stencil_clear_t );

    cmd_args->texture = texture;
    cmd_args->clear = clear_value;
}

void xg_cmd_buffer_clear_buffer ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, xg_buffer_h buffer, uint32_t clear_value ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_buffer_clear_m, key, xg_cmd_buffer_clear_t );

    cmd_args->buffer = buffer;
    cmd_args->clear = clear_value;
}

void xg_cmd_buffer_start_debug_capture ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, xg_debug_capture_stop_time_e stop_time ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_start_debug_capture_m, key, xg_cmd_start_debug_capture_t );

    cmd_args->stop_time = stop_time;
}

void xg_cmd_buffer_stop_debug_capture ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    xg_cmd_buffer_record_cmd_noargs_m ( cmd_buffer, xg_cmd_stop_debug_capture_m, key );
}

void xg_cmd_buffer_begin_debug_region ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, const char* name, uint32_t color ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_begin_debug_region_m, key, xg_cmd_begin_debug_region_t );

    cmd_args->name = name;
    cmd_args->color_rgba = color;
}

void xg_cmd_buffer_end_debug_region ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    xg_cmd_buffer_record_cmd_noargs_m ( cmd_buffer, xg_cmd_end_debug_region_m, key );
}

void xg_cmd_buffer_write_timestamp ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, const xg_cmd_query_timestamp_params_t* params ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_write_timestamp_m, key, xg_cmd_write_timestamp_t );

    cmd_args->pool = params->pool;
    cmd_args->idx = params->idx;
    cmd_args->stage = params->stage;
}

void xg_cmd_buffer_reset_query_pool ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, xg_query_pool_h pool ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_reset_query_pool_m, key, xg_query_pool_h );
    *cmd_args = pool;
}

void xg_cmd_build_raytrace_geometry ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, xg_raytrace_geometry_h geo, xg_buffer_h scratch ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_build_raytrace_geometry_m, key, xg_cmd_build_raytrace_geometry_t );
    cmd_args->geo = geo;
    cmd_args->scratch_buffer = scratch;
}

void xg_cmd_build_raytrace_world ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key, xg_raytrace_world_h world, xg_buffer_h scratch, xg_buffer_h instances ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_build_raytrace_world_m, key, xg_cmd_build_raytrace_world_t );
    cmd_args->world = world;
    cmd_args->scratch_buffer = scratch;
    cmd_args->instance_buffer = instances;
}
