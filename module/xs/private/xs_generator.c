#include "xs_generator.h"

typedef struct {
    std_virtual_stack_t stack;
} xs_generator_generation_context_t;

#if 0
static bool xs_generator_write_binding ( xs_generator_generation_context_t* context, const xg_resource_binding_layout_t* binding ) {
}

static void xs_generator_generation_context_init ( xs_generator_generation_context_t* context ) {
    std_alloc_t alloc = std_virtual_heap_alloc ( 1024 * 1024 * 4 ); // TODO define
    context->memory_handle = alloc.handle;
    context->stack = std_stack ( alloc.buffer );
}

static void xs_generator_generation_context_deinit ( xs_generator_generation_context_t* context ) { 
    std_virtual_buffer_free ( &context->buffer );
}

bool xs_generator_generate_graphics_bindings ( xs_parser_graphics_pipeline_state_t* state, const char* path ) {
    xs_generator_generation_context_t context;
    xs_generator_generation_context_init ( &context );

    const xg_resource_bindings_layout_t* layout = &state->params.resource_bindings;

    for ( size_t i = 0; i < layout->binding_points_count; ++i ) {
        const xg_resource_binding_layout_t* binding = &layout->binding_points[i];
        if ( !xs_generator_write_binding ( &context, binding ) ) {
            xs_generator_generation_context_deinit ( &context );
            return false;
        }
    }

    // TODO write file here

    xs_generator_generation_context_deinit ( &context );
    return true;
}
#endif
