#include "xg_cmd_buffer.h"

#include <std_mutex.h>
#include <std_list.h>
#include <std_queue.h>
#include <std_hash.h>

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

int xg_vk_cmd_buffer_header_compare_f ( const void* _a, const void* _b ) {
    xg_cmd_header_t* a = ( xg_cmd_header_t* ) _a;
    xg_cmd_header_t* b = ( xg_cmd_header_t* ) _b;
    return a->key > b->key ? 1 : a->key < b->key ? -1 : 0;
}

// STATIC

static xg_cmd_buffer_state_t* xg_cmd_buffer_state;

static void xg_cmd_buffer_alloc_memory ( xg_cmd_buffer_t* cmd_buffer ) {
    void* cmd_headers_buffer = std_virtual_heap_alloc ( xg_cmd_buffer_cmd_buffer_size_m, 16 );
    void* cmd_args_buffer = std_virtual_heap_alloc ( xg_cmd_buffer_cmd_buffer_size_m, 16 );
    cmd_buffer->cmd_headers_allocator = std_queue_local ( cmd_headers_buffer, xg_cmd_buffer_cmd_buffer_size_m );
    cmd_buffer->cmd_args_allocator = std_queue_local ( cmd_args_buffer, xg_cmd_buffer_cmd_buffer_size_m );
}

static void xg_cmd_buffer_free_memory ( xg_cmd_buffer_t* cmd_buffer ) {
    std_virtual_heap_free ( cmd_buffer->cmd_headers_allocator.base );
    std_virtual_heap_free ( cmd_buffer->cmd_args_allocator.base );
}

void xg_cmd_buffer_load ( xg_cmd_buffer_state_t* state ) {
    xg_cmd_buffer_state = state;

    // The pool is used to store all the xg_cmd_buffer_t items that eventually get used by the user.
    // Command Buffers contain poiners to the actual memory, which is allocated somewhere else.
    // xg_cmd_buffer_preallocated_cmd_buffers_m is the number of buffers that have their memory allocated from the start.
    // Once the pool runs out of buffers and more are requestd, it can grow, by lazily allocating more buffers.
    xg_cmd_buffer_state->cmd_buffers_array = std_virtual_heap_alloc_array_m ( xg_cmd_buffer_t, xg_cmd_buffer_max_cmd_buffers_m );
    xg_cmd_buffer_state->cmd_buffers_freelist = NULL;//std_freelist_m ( xg_cmd_buffer_state->cmd_buffers_array, sizeof ( xg_cmd_buffer_t ) );
    std_mutex_init ( &xg_cmd_buffer_state->cmd_buffers_mutex );

    for ( size_t i = 0; i < xg_cmd_buffer_preallocated_cmd_buffers_m; ++i ) {
        xg_cmd_buffer_t* cmd_buffer = &xg_cmd_buffer_state->cmd_buffers_array[i];
        xg_cmd_buffer_alloc_memory ( cmd_buffer );
        std_list_push ( &xg_cmd_buffer_state->cmd_buffers_freelist, cmd_buffer );
    }

    xg_cmd_buffer_state->allocated_cmd_buffers_count = xg_cmd_buffer_preallocated_cmd_buffers_m;

    if ( xg_cmd_buffer_cmd_buffer_size_m % std_virtual_page_size() != 0 ) {
        std_log_warn_m ( "Command buffer size is not a multiple of the system virtual page size, sub-optimal memory usage could result from this." );
    }
}

void xg_cmd_buffer_reload ( xg_cmd_buffer_state_t* state ) {
    xg_cmd_buffer_state = state;
}

void xg_cmd_buffer_unload ( void ) {
    for ( size_t i = 0; i < xg_cmd_buffer_state->allocated_cmd_buffers_count; ++i ) {
        xg_cmd_buffer_t* cmd_buffer = &xg_cmd_buffer_state->cmd_buffers_array[i];
        xg_cmd_buffer_free_memory ( cmd_buffer );
    }

    std_virtual_heap_free ( xg_cmd_buffer_state->cmd_buffers_array );
    std_mutex_deinit ( &xg_cmd_buffer_state->cmd_buffers_mutex );
}

// ======================================================================================= //
//                               C O M M A N D   B U F F E R
// ======================================================================================= //

void xg_cmd_buffer_open_n ( xg_cmd_buffer_h* cmd_buffer_handles, size_t count, xg_workload_h workload ) {
    std_mutex_lock ( &xg_cmd_buffer_state->cmd_buffers_mutex );

    for ( size_t i = 0; i < count; ++i ) {
        xg_cmd_buffer_t* cmd_buffer = std_list_pop_m ( &xg_cmd_buffer_state->cmd_buffers_freelist );

        if ( cmd_buffer == NULL ) {
            std_assert_m ( xg_cmd_buffer_state->allocated_cmd_buffers_count + 1 <= xg_cmd_buffer_max_cmd_buffers_m );
            std_log_info_m ( "Allocating additional command buffer " std_fmt_u64_m "/" std_fmt_int_m ", consider increasing xg_cmd_buffer_preallocated_cmd_buffers_m.", xg_cmd_buffer_state->allocated_cmd_buffers_count + 1, xg_cmd_buffer_max_cmd_buffers_m );
            // Buffers are linearly added to the pool. Therefore if freeing is allowed it must also be linear and back-to-front.
            cmd_buffer = &xg_cmd_buffer_state->cmd_buffers_array[xg_cmd_buffer_state->allocated_cmd_buffers_count++];
            xg_cmd_buffer_alloc_memory ( cmd_buffer );
        }

        cmd_buffer->workload = workload;

        xg_cmd_buffer_h cmd_buffer_handle = ( xg_cmd_buffer_h ) ( cmd_buffer - xg_cmd_buffer_state->cmd_buffers_array );
        cmd_buffer_handles[i] = cmd_buffer_handle;

        //xg_workload_add_cmd_buffer ( workload_handle, cmd_buffer_handle );
    }

    std_mutex_unlock ( &xg_cmd_buffer_state->cmd_buffers_mutex );
}

xg_cmd_buffer_h xg_cmd_buffer_open ( xg_workload_h workload ) {
    xg_cmd_buffer_h handle;
    xg_cmd_buffer_open_n ( &handle, 1, workload );
    return handle;
}

#if 0
void xg_cmd_buffer_close ( xg_cmd_buffer_h* cmd_buffer_handles, size_t count ) {
    // On close we store the cmd buffers in the shared queue
    // The user is expected to not use these anymore. Maybe it's a good idea to somehow mark the buffer as closed and test that on each user access?
    for ( size_t i = 0; i < count; ++i ) {
        xg_cmd_buffer_h handle = cmd_buffer_handles[i];
        xg_cmd_buffer_t* cmd_buffer = &xg_cmd_buffer_state.cmd_buffers_array[handle];

        xg_workload_add_cmd_buffer ( cmd_buffer->workload, handle );
    }
}
#endif

void xg_cmd_buffer_discard ( xg_cmd_buffer_h* cmd_buffer_handles, size_t count ) {
    std_mutex_lock ( &xg_cmd_buffer_state->cmd_buffers_mutex );

    for ( size_t i = 0; i < count; ++i ) {
        xg_cmd_buffer_h handle = cmd_buffer_handles[i];
        xg_cmd_buffer_t* cmd_buffer = &xg_cmd_buffer_state->cmd_buffers_array[handle];

        std_queue_local_clear ( &cmd_buffer->cmd_headers_allocator );
        std_queue_local_clear ( &cmd_buffer->cmd_args_allocator );

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

static void* xg_cmd_buffer_record_cmd ( xg_cmd_buffer_t* cmd_buffer, xg_cmd_type_e type, uint64_t key, size_t args_size ) {
    std_queue_local_align_push ( &cmd_buffer->cmd_headers_allocator, xg_cmd_buffer_cmd_alignment_m );
    std_queue_local_align_push ( &cmd_buffer->cmd_args_allocator, xg_cmd_buffer_cmd_alignment_m );

    xg_cmd_header_t* cmd_header = std_queue_local_emplace_array_m ( &cmd_buffer->cmd_headers_allocator, xg_cmd_header_t, 1 );
    void* cmd_args = NULL;

    if ( args_size ) {
        cmd_args = std_queue_local_emplace ( &cmd_buffer->cmd_args_allocator, args_size );
    }

    cmd_header->type = ( uint16_t ) type;
    cmd_header->args = ( uint64_t ) cmd_args;
    cmd_header->key = key;

    return cmd_args;
}
#define xg_cmd_buffer_record_cmd_m( cmd_buffer, cmd_type, key, args_type ) (args_type*) xg_cmd_buffer_record_cmd(cmd_buffer, cmd_type, key, sizeof(args_type))
#define xg_cmd_buffer_record_cmd_noargs_m( cmd_buffer, cmd_type, key )                  xg_cmd_buffer_record_cmd(cmd_buffer, cmd_type, key, 0)

// Graphics pipeline

void xg_cmd_buffer_graphics_streams_bind ( xg_cmd_buffer_h cmd_buffer_handle, const xg_vertex_stream_binding_t* bindings, size_t bindings_count, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_graphics_streams_bind_m, key, xg_cmd_graphics_streams_bind_t );

    std_assert_m ( bindings_count < UINT32_MAX );
    cmd_args->bindings_count = ( uint32_t ) bindings_count;
    std_queue_local_emplace_array_m ( &cmd_buffer->cmd_args_allocator, xg_vertex_stream_binding_t, bindings_count );

    for ( size_t i = 0; i < bindings_count; ++i ) {
        cmd_args->bindings[i] = bindings[i];
    }
}

void xg_cmd_buffer_graphics_streams_submit ( xg_cmd_buffer_h cmd_buffer_handle, size_t vertex_count, size_t vertex_base, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_graphics_streams_submit_m, key, xg_cmd_graphics_streams_submit_t );

    std_assert_m ( vertex_count < UINT32_MAX );
    std_assert_m ( vertex_base < UINT32_MAX );
    cmd_args->count = ( uint32_t ) vertex_count;
    cmd_args->base = ( uint32_t ) vertex_base;
}

void xg_cmd_buffer_graphics_streams_submit_indexed ( xg_cmd_buffer_h cmd_buffer_handle, xg_buffer_h ibuffer, size_t index_count, size_t ibuffer_base, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_graphics_streams_submit_indexed_m, key, xg_cmd_graphics_streams_submit_indexed_t );

    std_assert_m ( index_count < UINT32_MAX );
    std_assert_m ( ibuffer_base < UINT32_MAX );
    cmd_args->index_count = ( uint32_t ) index_count;
    cmd_args->ibuffer_base = ( uint32_t ) ibuffer_base;
    cmd_args->ibuffer = ibuffer;
}

void xg_cmd_buffer_graphics_streams_submit_instanced ( xg_cmd_buffer_h cmd_buffer_handle, size_t vertex_base, size_t vertex_count, size_t instance_base, size_t instance_count, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_graphics_streams_submit_instanced_m, key, xg_cmd_graphics_streams_submit_instanced_t );

    std_assert_m ( vertex_base < UINT32_MAX );
    std_assert_m ( vertex_count < UINT32_MAX );
    std_assert_m ( instance_base < UINT32_MAX );
    std_assert_m ( instance_count < UINT32_MAX );
    cmd_args->vertex_base = ( uint32_t ) vertex_base;
    cmd_args->vertex_count = ( uint32_t ) vertex_count;
    cmd_args->instance_base = ( uint32_t ) instance_base;
    cmd_args->instance_count = ( uint32_t ) instance_count;
}

void xg_cmd_buffer_graphics_pipeline_state_bind ( xg_cmd_buffer_h cmd_buffer_handle, xg_graphics_pipeline_state_h pipeline, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_graphics_pipeline_state_bind_m, key, xg_cmd_graphics_pipeline_state_bind_t );

    cmd_args->pipeline = pipeline;
}

void xg_cmd_buffer_graphics_pipeline_state_set_viewport ( xg_cmd_buffer_h cmd_buffer_handle, const xg_viewport_state_t* viewport, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_graphics_pipeline_state_set_viewport_m, key, xg_cmd_graphics_pipeline_state_set_viewport_t );

    cmd_args->viewport = *viewport;
}

void xg_cmd_buffer_graphics_pipeline_state_set_scissor ( xg_cmd_buffer_h cmd_buffer_handle, const xg_scissor_state_t* scissor, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_graphics_pipeline_state_set_scissor_m, key, xg_cmd_graphics_pipeline_state_set_scissor_t );

    cmd_args->scissor = *scissor;
}

void xg_cmd_buffer_pipeline_constant_write ( xg_cmd_buffer_h cmd_buffer_handle, const xg_pipeline_constant_data_t* constant_data, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_pipeline_constant_write_m, key, xg_cmd_pipeline_constant_data_write_t );

    cmd_args->stages = constant_data->stages;
    cmd_args->offset = constant_data->write_offset;
    cmd_args->size = constant_data->size;

    char* data = std_queue_local_emplace ( &cmd_buffer->cmd_args_allocator, constant_data->size );
    std_mem_copy ( data, constant_data->base, constant_data->size );
}

void xg_cmd_buffer_pipeline_resource_group_bind ( xg_cmd_buffer_h cmd_buffer_handle, xg_resource_binding_set_e set, xg_pipeline_resource_group_h group, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_pipeline_resource_group_bind_m, key, xg_cmd_pipeline_resource_group_bind_t );

    cmd_args->set = set;
    cmd_args->group = group;
}

void xg_cmd_buffer_pipeline_resources_bind ( xg_cmd_buffer_h cmd_buffer_handle, const xg_pipeline_resource_bindings_t* bindings, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_pipeline_resource_bind_m, key, xg_cmd_pipeline_resource_bind_t );

    xg_resource_binding_set_e set = bindings->set;
    uint32_t buffer_count = bindings->buffer_count;
    uint32_t texture_count = bindings->texture_count;
    uint32_t sampler_count = bindings->sampler_count;

    cmd_args->set = set;
    cmd_args->buffer_count = buffer_count;
    cmd_args->texture_count = texture_count;
    cmd_args->sampler_count = sampler_count;

    if ( buffer_count > 0 ) {
        std_queue_local_align_push ( &cmd_buffer->cmd_args_allocator, std_alignof_m ( xg_buffer_resource_binding_t ) );
        xg_buffer_resource_binding_t* buffers = ( xg_buffer_resource_binding_t* ) std_queue_local_emplace_array_m ( &cmd_buffer->cmd_args_allocator, xg_buffer_resource_binding_t, buffer_count );

        for ( uint32_t i = 0; i < buffer_count; ++i ) {
            buffers[i] = bindings->buffers[i];
        }
    }

    if ( texture_count > 0 ) {
        std_queue_local_align_push ( &cmd_buffer->cmd_args_allocator, std_alignof_m ( xg_texture_resource_binding_t ) );
        xg_texture_resource_binding_t* textures = ( xg_texture_resource_binding_t* ) std_queue_local_emplace_array_m ( &cmd_buffer->cmd_args_allocator, xg_texture_resource_binding_t, texture_count );

        for ( uint32_t i = 0; i < texture_count; ++i ) {
            textures[i] = bindings->textures[i];
        }
    }

    if ( sampler_count > 0 ) {
        std_queue_local_align_push ( &cmd_buffer->cmd_args_allocator, std_alignof_m ( xg_sampler_resource_binding_t ) );
        xg_sampler_resource_binding_t* samplers = ( xg_sampler_resource_binding_t* ) std_queue_local_emplace_array_m ( &cmd_buffer->cmd_args_allocator, xg_sampler_resource_binding_t, sampler_count );

        for ( uint32_t i = 0; i < sampler_count; ++i ) {
            samplers[i] = bindings->samplers[i];
        }
    }

    /*
        Old notes about pipeline resource bind:
        Here one decision has to be made: whether to immediately allocate/write the resource handles to the VK descriptor set or to keep them stored in cpu mem and delay the write to when the cmd buffer gets processed. The first solution means writing now and copying later, the second means nothing now and a write later. So it basically comes down to the cost of one vs the other?
        Machinery seems to be doing the write->copy one, not sure why. I will do the nothing->write for now, might re-evaluate/revisit this later. EDIT: big advantage of their write->copy is that to schedule the copy you just write the binder handle to the cmd buffer instead of the entire binder content. Given that you bind lots of stuff (and then probably sort/move it around) it probably makes sense to add the indirection.

        A Machinery resource binder is basically a heap. The layout is already defined in the pipeline state so nothing more is needed on that side. The binder owns the heap and takes the resource handles to bind. Finally the cmd buffer takes just the binder handle and on render time binds it (thus binds the resources that were attached to it).

        once filled a heap life is bound to that of the cmd buffer that contains a reference to it. Only one cmd buffer can reference one heap at a time. So the life of a heap is limited to one frame. so bsically: { get heap -> fill it linearly -> attach it to a cmd buffer/frame -> recycle it at end of frame }. two operations on heaps from the user: write descriptors to it, and bind descriptors written to it to a cmd buffer.
            binder.bind(segment)
            cmd_buffer.bind(binder)

            cmd_buffer.add_binder(binder)
                cmd_buffer.take_ownership(binder.heap)
            binder.bind(segment)
                heap.allocate(segment.resources)
            binder.bind(segment)
                heap.allocate(segment.resources)
            binder.bind(segment)
                heap.allocate(segment.resources)
            ...

        resource_binder:
            owns descriptor heap
            can create descriptor sets (taking a pipeline as param?)


        Nothing->write means that the bindings segments can be compressed in one single block [counts] [handles] instead of multiple blocks [type] [handles]. It is not a massive compression though, it encodes the type in the counts index but always writes all counts and it avois to waste a few bits on multiple bindings segments of the same type.
    */
}

void xg_cmd_buffer_graphics_render_textures_bind ( xg_cmd_buffer_h cmd_buffer_handle, const xg_render_textures_binding_t* bindings, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_graphics_render_textures_bind_m, key, xg_cmd_graphics_render_texture_bind_t );

    cmd_args->depth_stencil = bindings->depth_stencil;
    cmd_args->render_targets_count = bindings->render_targets_count;
    std_queue_local_emplace_array_m ( &cmd_buffer->cmd_args_allocator, xg_render_target_binding_t, bindings->render_targets_count );

    for ( size_t i = 0 ; i < bindings->render_targets_count; ++i ) {
        cmd_args->render_targets[i] = bindings->render_targets[i];
    }
}

void xg_cmd_buffer_compute_dispatch ( xg_cmd_buffer_h cmd_buffer_handle, uint32_t workgroup_count_x, uint32_t workgroup_count_y, uint32_t workgroup_count_z, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_compute_dispatch_m, key, xg_cmd_compute_dispatch_t );

    cmd_args->workgroup_count_x = workgroup_count_x;
    cmd_args->workgroup_count_y = workgroup_count_y;
    cmd_args->workgroup_count_z = workgroup_count_z;
}

void xg_cmd_buffer_compute_pipeline_state_bind ( xg_cmd_buffer_h cmd_buffer_handle, xg_graphics_pipeline_state_h pipeline, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_compute_pipeline_state_bind_m, key, xg_cmd_compute_pipeline_state_bind_t );

    cmd_args->pipeline = pipeline;
}

void xg_cmd_buffer_copy_buffer ( xg_cmd_buffer_h cmd_buffer_handle, xg_buffer_h source, xg_buffer_h dest, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_copy_buffer_m, key, xg_cmd_copy_buffer_t );

    cmd_args->source = source;
    cmd_args->destination = dest;
}

void xg_cmd_buffer_copy_texture ( xg_cmd_buffer_h cmd_buffer_handle, const xg_texture_copy_params_t* params, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_copy_texture_m, key, xg_cmd_copy_texture_t );

    cmd_args->source = params->source;
    cmd_args->destination = params->destination;
    cmd_args->mip_count = params->mip_count;
    cmd_args->array_count = params->array_count;
    cmd_args->aspect = params->aspect;
    cmd_args->filter = params->filter;
}

void xg_cmd_buffer_copy_buffer_to_texture ( xg_cmd_buffer_h cmd_buffer_handle, const xg_buffer_to_texture_copy_params_t* params, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_copy_buffer_to_texture_m, key, xg_cmd_copy_buffer_to_texture_t );

    cmd_args->source = params->source;
    cmd_args->source_offset = params->source_offset;
    cmd_args->destination = params->destination;
    cmd_args->mip_base = params->mip_base;
    cmd_args->array_base = params->array_base;
    cmd_args->array_count = params->array_count;
    //cmd_args->aspect = params->aspect;
    //cmd_args->layout = params->layout;
}

void xg_cmd_buffer_copy_texture_to_buffer ( xg_cmd_buffer_h cmd_buffer_handle, const xg_texture_to_buffer_copy_params_t* params, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_copy_texture_to_buffer_m, key, xg_cmd_copy_texture_to_buffer_t );

    cmd_args->destination = params->destination;
    cmd_args->destination_offset = params->destination_offset;
    cmd_args->source = params->source;
    cmd_args->mip_base = params->mip_base;
    cmd_args->array_base = params->array_base;
    cmd_args->array_count = params->array_count;
}

void xg_cmd_buffer_barrier_set ( xg_cmd_buffer_h cmd_buffer_handle, const xg_barrier_set_t* barrier_set, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_barrier_set_m, key, xg_cmd_barrier_set_t );

    //cmd_args->execution_barrier = barrier_set->execution_barrier;
    cmd_args->memory_barriers = ( uint32_t ) barrier_set->memory_barriers_count;
    cmd_args->buffer_memory_barriers = ( uint32_t ) barrier_set->buffer_memory_barriers_count;
    cmd_args->texture_memory_barriers = ( uint32_t ) barrier_set->texture_memory_barriers_count;

    if ( barrier_set->memory_barriers_count > 0 ) {
        std_queue_local_align_push ( &cmd_buffer->cmd_args_allocator, std_alignof_m ( xg_memory_barrier_t ) );
        xg_memory_barrier_t* barriers = std_queue_local_emplace_array_m ( &cmd_buffer->cmd_args_allocator, xg_memory_barrier_t, barrier_set->memory_barriers_count );
        std_mem_copy_array_m ( barriers, &barrier_set->memory_barriers, barrier_set->memory_barriers_count );
    }

    // TODO assert that all texture barriers have valid subresources (e.g. mip not set to xg_texture_all_mips_m)

    if ( barrier_set->buffer_memory_barriers_count > 0 ) {
        std_queue_local_align_push ( &cmd_buffer->cmd_args_allocator, std_alignof_m ( xg_buffer_memory_barrier_t ) );
        xg_buffer_memory_barrier_t* barriers = std_queue_local_emplace_array_m ( &cmd_buffer->cmd_args_allocator, xg_buffer_memory_barrier_t, barrier_set->buffer_memory_barriers_count );
        std_mem_copy_array_m ( barriers, barrier_set->buffer_memory_barriers, barrier_set->buffer_memory_barriers_count );
    }

    if ( barrier_set->texture_memory_barriers_count > 0 ) {
        std_queue_local_align_push ( &cmd_buffer->cmd_args_allocator, std_alignof_m ( xg_texture_memory_barrier_t ) );
        xg_texture_memory_barrier_t* barriers = std_queue_local_emplace_array_m ( &cmd_buffer->cmd_args_allocator, xg_texture_memory_barrier_t, barrier_set->texture_memory_barriers_count );
        std_mem_copy_array_m ( barriers, barrier_set->texture_memory_barriers, barrier_set->texture_memory_barriers_count );
    }
}

void xg_cmd_buffer_texture_clear ( xg_cmd_buffer_h cmd_buffer_handle, xg_texture_h texture, xg_color_clear_t clear_color, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_texture_clear_m, key, xg_cmd_texture_clear_t );

    cmd_args->texture = texture;
    cmd_args->clear = clear_color;
}

void    xg_cmd_buffer_texture_depth_stencil_clear ( xg_cmd_buffer_h cmd_buffer_handle, xg_texture_h texture, xg_depth_stencil_clear_t clear_value, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_texture_depth_stencil_clear_m, key, xg_cmd_texture_depth_stencil_clear_t );

    cmd_args->texture = texture;
    cmd_args->clear = clear_value;
}

void xg_cmd_buffer_start_debug_capture ( xg_cmd_buffer_h cmd_buffer_handle, xg_debug_capture_stop_time_e stop_time, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_start_debug_capture_m, key, xg_cmd_start_debug_capture_t );

    cmd_args->stop_time = stop_time;
}

void xg_cmd_buffer_stop_debug_capture ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    xg_cmd_buffer_record_cmd_noargs_m ( cmd_buffer, xg_cmd_stop_debug_capture_m, key );
}

void xg_cmd_buffer_begin_debug_region ( xg_cmd_buffer_h cmd_buffer_handle, const char* name, uint32_t color, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_begin_debug_region_m, key, xg_cmd_begin_debug_region_t );

    cmd_args->name = name;
    cmd_args->color_rgba = color;
}

void xg_cmd_buffer_end_debug_region ( xg_cmd_buffer_h cmd_buffer_handle, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    xg_cmd_buffer_record_cmd_noargs_m ( cmd_buffer, xg_cmd_end_debug_region_m, key );
}

#if 0
void xg_cmd_buffer_write_timestamp ( xg_cmd_buffer_h cmd_buffer_handle, const xg_timestamp_query_params_t* params, uint64_t key ) {
    xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_cmd_buffer_record_cmd_m ( cmd_buffer, xg_cmd_write_timestamp_m, key, xg_cmd_write_timestamp_t );

    cmd_args->query_buffer = params->buffer;
    cmd_args->dependency = params->dependency;
}
#endif
