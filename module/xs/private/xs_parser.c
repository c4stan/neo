#include "xs_parser.h"

#include <std_hash.h>
#include <std_log.h>
#include <std_file.h>

#include "xs_database.h"

#define xs_parser_line_comment_token_m "//"
#define xs_parser_multiline_comment_begin_token_m "/*"
#define xs_parser_multiline_comment_end_token_m "*/"

typedef struct {
    const char* head;
    const char* begin;
    const char* eof;
    size_t line_count;
    const char* line_begin;

    const char* path;
    void* state; // xs_parser_graphics_pipeline_state_t* / compute / ...

#if 0
    xg_pipeline_e pipeline_type;
    union {
        xs_parser_graphics_pipeline_state_t* graphics_state;
        xs_parser_compute_pipeline_state_t* compute_state;
    };
#else
    xg_pipeline_e pipeline_type;
    union {
        xg_graphics_pipeline_params_t* graphics_pipeline;
        xg_compute_pipeline_params_t* compute_pipeline;
        xg_raytrace_pipeline_params_t* raytrace_pipeline;
    };

    xs_parser_shader_references_t* shader_references;
    xs_parser_shader_definitions_t* shader_definitions;
    xg_resource_bindings_layout_params_t* resource_layouts;
#endif
} xs_parser_parsing_context_t;

static xg_shading_stage_bit_e xs_parser_default_shading_stage ( xs_parser_parsing_context_t* context ) {
    switch ( context->pipeline_type ) {
    case xg_pipeline_graphics_m:
        return xg_shading_stage_bit_vertex_m | xg_shading_stage_bit_fragment_m;
    case xg_pipeline_compute_m:
        return xg_shading_stage_bit_compute_m;
    case xg_pipeline_raytrace_m:
        return xg_shading_stage_bit_ray_gen_m; // TODO is this right?
    }
}

static size_t xs_parser_skip_to_text ( xs_parser_parsing_context_t* context ) {
    const char* begin = context->head;

    while ( context->head < context->eof ) {
        if ( *context->head == '\n' ) {
            ++context->line_count;
            context->line_begin = context->head;
            ++context->head;
        } else if ( *context->head == '\r' || *context->head == ' ' ) {
            ++context->head;
        } else {
            break;
        }
    }

    return ( size_t ) ( context->head - begin );
}

static size_t xs_parser_skip_spaces ( xs_parser_parsing_context_t* context ) {
    const char* begin = context->head;

    while ( context->head < context->eof ) {
        if ( *context->head == ' ' ) {
            ++context->head;
        } else {
            break;
        }
    }

    return ( size_t ) ( context->head - begin );
}

static size_t xs_parser_skip_line ( xs_parser_parsing_context_t* context ) {
    const char* begin = context->head;

    if ( context->head < context->eof && *context->head == '\n' ) {
        ++context->head;
    }

    while ( context->head < context->eof ) {
        if ( *context->head != '\n' ) {
            ++context->head;
        } else {
            context->line_begin = context->head;
            break;
        }
    }

    return ( size_t ) ( context->head - begin );
}

static size_t xs_parser_skip_comment_block ( xs_parser_parsing_context_t* context ) {
    const char* begin = context->head;

    while ( context->head < context->eof ) {
        if ( std_str_starts_with ( context->head, xs_parser_multiline_comment_end_token_m ) ) {
            context->head += 2;
            break;
        }

        ++context->head;
    }

    return ( size_t ) ( context->head - begin );
}

std_unused_static_m()
static size_t xs_parser_skip_block ( xs_parser_parsing_context_t* context ) {
    const char* begin = context->head;

    int32_t level = 1;

    while ( context->head < context->eof ) {
        if ( std_str_starts_with ( context->head, "begin" ) ) {
            context->head += 5;
            level += 1;
        }

        if ( std_str_starts_with ( context->head, "end" ) ) {
            context->head += 3;
            level -= 1;

            if ( level == 0 ) {
                break;
            }
        }

        ++context->head;
    }

    return ( size_t ) ( context->head - begin );
}

static size_t xs_parser_read_u32 ( xs_parser_parsing_context_t* context, uint32_t* out ) {
    const char* begin = context->head;
    uint32_t result = 0;

    while ( context->head < context->eof && *context->head >= '0' && *context->head <= '9' ) {
        result = result * 10 + *context->head - '0';
        ++context->head;
    }

    std_assert_m ( *context->head == ' ' || *context->head == '\n' || *context->head == '\r' );

    *out = result;

    return ( size_t ) ( context->head - begin );
}

std_warnings_save_state_m()
std_warnings_ignore_m ( "-Wunused-function" )
static size_t xs_parser_read_i32 ( xs_parser_parsing_context_t* context, int32_t* out ) {
    const char* begin = context->head;
    int result = 0;
    int sign = 1;

    while ( context->head < context->eof && ( *context->head == '-' || *context->head == '+' ) ) {
        if ( *context->head == '-' ) {
            sign = sign * -1;
        }

        ++context->head;
    }

    while ( context->head < context->eof && *context->head >= '0' && *context->head <= '9' ) {
        result = result * 10 + *context->head - '0';
        ++context->head;
    }

    std_assert_m ( *context->head == ' ' || *context->head == '\n' || *context->head == '\r' );

    *out = result * sign;

    return ( size_t ) ( context->head - begin );
}
std_warnings_restore_state_m()

static const char* xs_parser_peek ( xs_parser_parsing_context_t* context ) {
    return context->head;
}

// TODO fail if the string can't fit the buffer instead of doing a silent truncate
static size_t xs_parser_read_word ( xs_parser_parsing_context_t* context, char* dest, size_t cap ) {
    const char* begin = context->head;

    while ( context->head < context->eof && *context->head != ' ' && *context->head != '\n' && *context->head != '\r'  ) {
        if ( ( size_t ) ( context->head - begin ) == cap ) {
            break;
        }

        *dest = *context->head;
        ++dest;
        ++context->head;
    }

    *dest = '\0';

    return ( size_t ) ( context->head - begin );
}

std_warnings_save_state_m()
std_warnings_ignore_m ( "-Wunused-function" )
static size_t xs_parser_extract_trailing_u32 ( const char* str, size_t len, uint32_t* out ) {
    while ( len > 0 && str[len - 1] >= '0' && str[len - 1] <= '9' ) {
        --len;
    }

    const char* begin = &str[len];
    str += len;
    uint32_t result = 0;

    while ( *str >= '0' && *str <= '9' ) {
        result = result * 10 + *str - '0';
        ++str;
    }

    //std_assert_m ( *str == ' ' || *str == '\n' );

    *out = result;

    return ( size_t ) ( str - begin );
}
std_warnings_restore_state_m()

static bool xs_parser_str_to_bool ( const char* token ) {
    if ( std_str_cmp ( token, "on" ) == 0 || std_str_cmp ( token, "true" ) == 0 ) {
        return true;
    } else if ( std_str_cmp ( token, "off" ) == 0 || std_str_cmp ( token, "false" ) == 0 ) {
        return false;
    } else {
        std_assert_m ( false );
        return false;
    }
}

static xg_format_e xs_parser_format_to_enum ( const char* format ) {

    if ( std_str_cmp ( format, "R8_UINT" ) == 0 ) {
        return xg_format_r8_uint_m;
    }

    else if ( std_str_cmp ( format, "R16_FLOAT" ) == 0 ) {
        return xg_format_r16_sfloat_m;
    } else if ( std_str_cmp ( format, "R16_UNORM" ) == 0 ) {
        return xg_format_r16_unorm_m;
    } else if ( std_str_cmp ( format, "R32_FLOAT" ) == 0 ) {
        return xg_format_r32_sfloat_m;

    } else if ( std_str_cmp ( format, "R16G16_FLOAT" ) == 0 ) {
        return xg_format_r16g16_sfloat_m;
    } else if ( std_str_cmp ( format, "R16G16_UNORM" ) == 0 ) {
        return xg_format_r16g16_unorm_m;
    } else if ( std_str_cmp ( format, "R32G32_FLOAT" ) == 0 ) {
        return xg_format_r32g32_sfloat_m;

    } else if ( std_str_cmp ( format, "R16G16B16_FLOAT" ) == 0 ) {
        return xg_format_r16g16b16_sfloat_m;
    } else if ( std_str_cmp ( format, "R16G16B16_UNORM" ) == 0 ) {
        return xg_format_r16g16b16_unorm_m;
    } else if ( std_str_cmp ( format, "R32G32B32_FLOAT" ) == 0 ) {
        return xg_format_r32g32b32_sfloat_m;

    } else if ( std_str_cmp ( format, "R16G16B16A16_FLOAT" ) == 0 ) {
        return xg_format_r16g16b16a16_sfloat_m;
    } else if ( std_str_cmp ( format, "R16G16B16A16_UNORM" ) == 0 ) {
        return xg_format_r16g16b16a16_unorm_m;
    } else if ( std_str_cmp ( format, "R32G32B32A32_FLOAT" ) == 0 ) {
        return xg_format_r32g32b32a32_sfloat_m;

    } else if ( std_str_cmp ( format, "B8G8R8A8_UNORM" ) == 0 ) {
        return xg_format_b8g8r8a8_unorm_m;
    } else if ( std_str_cmp ( format, "B8G8R8A8_SRGB" ) == 0 ) {
        return xg_format_b8g8r8a8_srgb_m;
    } else if ( std_str_cmp ( format, "R8G8B8A8_UNORM" ) == 0 ) {
        return xg_format_r8g8b8a8_unorm_m;
    } else if ( std_str_cmp ( format, "R8G8B8A8_SRGB" ) == 0 ) {
        return xg_format_r8g8b8a8_srgb_m;
    } else if ( std_str_cmp ( format, "R8G8B8A8_UINT" ) == 0 ) {
        return xg_format_r8g8b8a8_uint_m;

    } else if ( std_str_cmp ( format, "R5G6B5_UNORM" ) == 0 ) {
        return xg_format_r5g6b5_unorm_pack16_m;

    } else if ( std_str_cmp ( format, "B10G11R11_UFLOAT" ) == 0 ) {
        return xg_format_b10g11r11_ufloat_pack32_m;

    } else if ( std_str_cmp ( format, "A2B10G10R10_UNORM" ) == 0 ) {
        return xg_format_a2b10g10r10_unorm_pack32_m;

    } else if ( std_str_cmp ( format, "D16_UNORM" ) == 0 ) {
        return xg_format_d16_unorm_m;
    } else if ( std_str_cmp ( format, "D24_UNORM_S8_UINT" ) == 0 ) {
        return xg_format_d24_unorm_s8_uint_m;
    } else if ( std_str_cmp ( format, "D32_SFLOAT" ) == 0 ) {
        return xg_format_d32_sfloat_m;
    }

    std_assert_m ( false );
    return xg_format_undefined_m;
}

static xg_blend_factor_e xs_parser_blend_factor_to_enum ( const char* factor ) {
    if ( std_str_cmp ( factor, "zero" ) == 0 ) {
        return xg_blend_factor_zero_m;
    } else if ( std_str_cmp ( factor, "one" ) == 0 ) {
        return xg_blend_factor_one_m;
    } else if ( std_str_cmp ( factor, "src_color" ) == 0 ) {
        return xg_blend_factor_src_color_m;
    } else if ( std_str_cmp ( factor, "one_minus_src_color" ) == 0 ) {
        return xg_blend_factor_one_minus_src_color_m;
    } else if ( std_str_cmp ( factor, "dst_color" ) == 0 ) {
        return xg_blend_factor_dst_color_m;
    } else if ( std_str_cmp ( factor, "one_minus_dst_color" ) == 0 ) {
        return xg_blend_factor_one_minus_dst_color_m;
    } else if ( std_str_cmp ( factor, "src_alpha" ) == 0 ) {
        return xg_blend_factor_src_alpha_m;
    } else if ( std_str_cmp ( factor, "one_minus_src_alpha" ) == 0 ) {
        return xg_blend_factor_one_minus_src_alpha_m;
    } else if ( std_str_cmp ( factor, "dst_alpha" ) == 0 ) {
        return xg_blend_factor_dst_alpha_m;
    } else if ( std_str_cmp ( factor, "one_minus_dst_alpha" ) == 0 ) {
        return xg_blend_factor_one_minus_dst_alpha_m;
    }

    std_assert_m ( false );
    return xg_blend_factor_zero_m;
}

static xg_blend_op_e xs_parser_blend_op_to_enum ( const char* op ) {
    if ( std_str_cmp ( op, "add" ) == 0 ) {
        return xg_blend_op_add_m;
    } else if ( std_str_cmp ( op, "subtract" ) == 0 ) {
        return xg_blend_op_subtract_m;
    } else if ( std_str_cmp ( op, "reverse_subtract" ) == 0 ) {
        return xg_blend_op_reverse_subtract_m;
    } else if ( std_str_cmp ( op, "min" ) == 0 ) {
        return xg_blend_op_min_m;
    } else if ( std_str_cmp ( op, "max" ) == 0 ) {
        return xg_blend_op_max_m;
    }

    std_assert_m ( false );
    return xg_blend_op_add_m;
}

static xg_compare_op_e xs_parser_compare_op_to_enum ( const char* op ) {
    if ( std_str_cmp ( op, "never" ) == 0 ) {
        return xg_compare_op_never_m;
    } else if ( std_str_cmp ( op, "less" ) == 0 ) {
        return xg_compare_op_less_m;
    } else if ( std_str_cmp ( op, "equal" ) == 0 ) {
        return xg_compare_op_equal_m;
    } else if ( std_str_cmp ( op, "less_or_equal" ) == 0 ) {
        return xg_compare_op_less_or_equal_m;
    } else if ( std_str_cmp ( op, "greater" ) == 0 ) {
        return xg_compare_op_greater_m;
    } else if ( std_str_cmp ( op, "not_equal" ) == 0 ) {
        return xg_compare_op_not_equal_m;
    } else if ( std_str_cmp ( op, "greater_or_equal" ) == 0 ) {
        return xg_compare_op_greater_or_equal_m;
    } else if ( std_str_cmp ( op, "always" ) == 0 ) {
        return xg_compare_op_always_m;
    }

    std_assert_m ( false );
    return xg_compare_op_never_m;
}

static xg_shader_binding_set_e xs_parser_shader_binding_set_to_enum ( const char* set ) {
    if ( std_str_cmp ( set, "workload" ) == 0 ) {
        return xg_shader_binding_set_workload_m;
    } else if ( std_str_cmp ( set, "pass" ) == 0 ) {
        return xg_shader_binding_set_pass_m;
    } else if ( std_str_cmp ( set, "material" ) == 0 ) {
        return xg_shader_binding_set_material_m;
    } else if ( std_str_cmp ( set, "dispatch" ) == 0 ) {
        return xg_shader_binding_set_dispatch_m;
    }

    std_assert_m ( false );
    return xg_shader_binding_set_invalid_m;
}

static xg_shading_stage_bit_e xs_parser_shading_stage_to_bit ( const char* stage ) {
    if ( std_str_cmp ( stage, "vertex" ) == 0 ) {
        return xg_shading_stage_bit_vertex_m;
    } else if ( std_str_cmp ( stage, "fragment" ) == 0 || std_str_cmp ( stage, "pixel" ) == 0 ) {
        return xg_shading_stage_bit_fragment_m;
    } else if ( std_str_cmp ( stage, "compute" ) == 0 ) {
        return xg_shading_stage_bit_compute_m;
    } else if ( std_str_cmp ( stage, "ray_gen" ) == 0 ) {
        return xg_shading_stage_bit_ray_gen_m;
    } else if ( std_str_cmp ( stage, "ray_miss" ) == 0 ) {
        return xg_shading_stage_bit_ray_miss_m;
    } else if ( std_str_cmp ( stage, "ray_hit_closest" ) == 0 ) {
        return xg_shading_stage_bit_ray_hit_closest_m;
    }

    return xg_shading_stage_bit_none_m;
}

static void xs_parser_parse_input_desc ( xs_parser_parsing_context_t* context ) {
    /*if ( context->result->state.input_layout.count == xs_size_undefined_m ) {
        context->result->state.input_layout.count = 0;
    }*/

    char token[xs_shader_parser_max_token_size_m];

    //while ( context->head < context->eof ) {
    xg_vertex_stream_t* stream = &context->graphics_pipeline->state.input_layout.streams[context->graphics_pipeline->state.input_layout.stream_count];
    stream->attribute_count = 0;

    {
        xs_parser_skip_to_text ( context );
        uint32_t id;
        size_t len = xs_parser_read_u32 ( context, &id );
        std_assert_m ( len > 0 );
        /*
        if ( len == 0 ) {
            xs_parser_skip_to_text ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            std_assert_m ( std_str_cmp ( token, "end" ) == 0 );
            //break;
            return;
        }
        */
        stream->id = id;
    }

    while ( context->head < context->eof ) {//&& *context->head != '\n' ) {
        xg_vertex_attribute_t* attribute = &stream->attributes[stream->attribute_count];

        //xs_parser_skip_spaces ( context );
        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        if ( std_str_find ( token, "pos" ) == 0 ) {
            attribute->name = xg_vertex_attribute_pos_m;
        } else if ( std_str_find ( token, "nor" ) == 0 ) {
            attribute->name = xg_vertex_attribute_nor_m;
        } else if ( std_str_find ( token, "tan" ) == 0 ) {
            attribute->name = xg_vertex_attribute_tan_m;
        } else if ( std_str_find ( token, "uv" ) == 0 || std_str_find ( token, "tex" ) == 0 ) {
            attribute->name = xg_vertex_attribute_uv_m;
        } else if ( std_str_find ( token, "color" ) == 0 ) {
            attribute->name = xg_vertex_attribute_color_m;
        } else if ( std_str_cmp ( token, "end" ) == 0 ) {
            break;
        }

        //uint32_t id;
        //len = xs_parser_extract_trailing_u32 ( token, len, &id );
        //std_assert_m ( len > 0 );
        //attribute->id = id;
        attribute->id = stream->id;

        xs_parser_skip_spaces ( context );
        len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );
        xg_format_e format = xs_parser_format_to_enum ( token );
        std_assert_m ( format != xg_format_undefined_m );
        attribute->format = format;

        ++stream->attribute_count;
    }

    ++context->graphics_pipeline->state.input_layout.stream_count;
    //}
}

static void xs_parser_parse_viewport ( xs_parser_parsing_context_t* context ) {
    char token[xs_shader_parser_max_token_size_m];

    while ( context->head < context->eof ) {

        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        if ( std_str_cmp ( token, "min_depth" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            float f32 = std_str_to_f32 ( token );
            std_assert_m ( f32 >= 0 && f32 <= 1 ); // TODO is this right?
            context->graphics_pipeline->state.viewport_state.min_depth = f32;
        } else if ( std_str_cmp ( token, "max_depth" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            float f32 = std_str_to_f32 ( token );
            std_assert_m ( f32 >= 0 && f32 <= 1 ); // TODO is this right?
            context->graphics_pipeline->state.viewport_state.max_depth = f32;
        } else if ( std_str_cmp ( token, "width" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            uint32_t width;

            if ( *context->head == '0' ) {
                len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
                float scale = std_str_to_f32 ( token );
                width = ( uint32_t ) ( context->graphics_pipeline->state.viewport_state.width * scale + 0.01f );
            } else if ( *context->head == '/' ) {
                len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
                std_assert_m ( len == 1 );
                xs_parser_skip_spaces ( context );
                uint32_t div;
                len = xs_parser_read_u32 ( context, &div );
                width = context->graphics_pipeline->state.viewport_state.width / div;
            } else {
                len = xs_parser_read_u32 ( context, &width );
            }

            context->graphics_pipeline->state.viewport_state.width = width;
        } else if ( std_str_cmp ( token, "height" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            uint32_t height;

            if ( *context->head == '0' ) {
                len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
                float scale = std_str_to_f32 ( token );
                height = ( uint32_t ) ( context->graphics_pipeline->state.viewport_state.height * scale + 0.01f );
            } else if ( *context->head == '/' ) {
                len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
                std_assert_m ( len == 1 );
                xs_parser_skip_spaces ( context );
                uint32_t div;
                len = xs_parser_read_u32 ( context, &div );
                height = context->graphics_pipeline->state.viewport_state.height / div;
            } else {
                len = xs_parser_read_u32 ( context, &height );
            }

            context->graphics_pipeline->state.viewport_state.height = height;
        } else if ( std_str_cmp ( token, "dynamic" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            bool value = xs_parser_str_to_bool ( token );
            xg_graphics_pipeline_dynamic_state_bit_e state = context->graphics_pipeline->state.dynamic_state;
            state = std_bit_write_64_m ( state, xg_graphics_pipeline_dynamic_state_viewport_m, value );
            context->graphics_pipeline->state.dynamic_state = state;
        } else if ( std_str_cmp ( token, xs_parser_line_comment_token_m ) == 0 ) {
            xs_parser_skip_line ( context );
        } else {
            std_assert_m ( std_str_cmp ( token, "end" ) == 0 );
            break;
        }
    }
}

static void xs_parser_parse_scissor ( xs_parser_parsing_context_t* context ) {
    char token[xs_shader_parser_max_token_size_m];

    while ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        if ( std_str_cmp ( token, "x" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            uint32_t u32 = std_str_to_u32 ( token );
            context->graphics_pipeline->state.scissor_state.x = u32;
        } else if ( std_str_cmp ( token, "y" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            uint32_t u32 = std_str_to_u32 ( token );
            context->graphics_pipeline->state.scissor_state.y = u32;
        } else if ( std_str_cmp ( token, "width" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            uint32_t u32 = std_str_to_u32 ( token );
            context->graphics_pipeline->state.scissor_state.width = u32;
        } else if ( std_str_cmp ( token, "height" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            uint32_t u32 = std_str_to_u32 ( token );
            context->graphics_pipeline->state.scissor_state.height = u32;
        } else if ( std_str_cmp ( token, "dynamic" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            bool value = xs_parser_str_to_bool ( token );
            xg_graphics_pipeline_dynamic_state_bit_e state = context->graphics_pipeline->state.dynamic_state;
            state = std_bit_write_64_m ( state, xg_graphics_pipeline_dynamic_state_scissor_m, value );
            context->graphics_pipeline->state.dynamic_state = state;
        } else if ( std_str_cmp ( token, xs_parser_line_comment_token_m ) == 0 ) {
            xs_parser_skip_line ( context );
        } else {
            std_assert_m ( std_str_cmp ( token, "end" ) == 0 );
            break;
        }
    }
}

static void xs_parser_parse_rasterizer ( xs_parser_parsing_context_t* context ) {
    char token[xs_shader_parser_max_token_size_m];

    while ( context->head < context->eof ) {

        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        if ( std_str_cmp ( token, "depth_clamp" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            context->graphics_pipeline->state.rasterizer_state.enable_depth_clamp = xs_parser_str_to_bool ( token );
        } else if ( std_str_cmp ( token, "discard" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            context->graphics_pipeline->state.rasterizer_state.disable_rasterization = xs_parser_str_to_bool ( token );
        } else if ( std_str_cmp ( token, "polygon" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );

            if ( std_str_cmp ( token, "fill" ) == 0 ) {
                context->graphics_pipeline->state.rasterizer_state.polygon_mode = xg_polygon_mode_fill_m;
            } else if ( std_str_cmp ( token, "line" ) == 0 ) {
                context->graphics_pipeline->state.rasterizer_state.polygon_mode = xg_polygon_mode_line_m;
            } else if ( std_str_cmp ( token, "point" ) == 0 ) {
                context->graphics_pipeline->state.rasterizer_state.polygon_mode = xg_polygon_mode_point_m;
            } else {
                std_assert_m ( false );
            }
        } else if ( std_str_cmp ( token, "line_width" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            float f32 = std_str_to_f32 ( token );
            std_assert_m ( f32 >= 0 );
            context->graphics_pipeline->state.rasterizer_state.line_width = f32;
        } else if ( std_str_cmp ( token, "cull" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );

            // TODO support both, it's a bitflag
            if ( std_str_cmp ( token, "front" ) == 0 ) {
                context->graphics_pipeline->state.rasterizer_state.cull_mode = xg_cull_mode_bit_front_m;
            } else if ( std_str_cmp ( token, "back" ) == 0 ) {
                context->graphics_pipeline->state.rasterizer_state.cull_mode = xg_cull_mode_bit_back_m;
            } else {
                context->graphics_pipeline->state.rasterizer_state.cull_mode = xg_cull_mode_bit_none_m;
            }
        } else if ( std_str_cmp ( token, "front" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );

            if ( std_str_cmp ( token, "clockwise" ) == 0 ) {
                context->graphics_pipeline->state.rasterizer_state.frontface_winding_mode = xg_winding_clockwise_m;
            } else if ( std_str_cmp ( token, "counterclockwise" ) == 0 ) {
                context->graphics_pipeline->state.rasterizer_state.frontface_winding_mode = xg_winding_counter_clockwise_m;
            } else {
                std_assert_m ( false );
            }
        } else if ( std_str_cmp ( token, "depth_bias" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            context->graphics_pipeline->state.rasterizer_state.depth_bias_state.enable = xs_parser_str_to_bool ( token );
        } else if ( std_str_cmp ( token, "depth_bias_const" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            float f32 = std_str_to_f32 ( token );
            context->graphics_pipeline->state.rasterizer_state.depth_bias_state.const_factor = f32;
        } else if ( std_str_cmp ( token, "depth_bias_clamp" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            float f32 = std_str_to_f32 ( token );
            context->graphics_pipeline->state.rasterizer_state.depth_bias_state.clamp = f32;
        } else if ( std_str_cmp ( token, "depth_bias_slope" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            float f32 = std_str_to_f32 ( token );
            context->graphics_pipeline->state.rasterizer_state.depth_bias_state.slope_factor = f32;
        } else if ( std_str_cmp ( token, xs_parser_line_comment_token_m ) == 0 ) {
            xs_parser_skip_line ( context );
        } else {
            std_assert_m ( std_str_cmp ( token, "end" ) == 0 );
            break;
        }
    }
}

/*
static void xs_parser_parse_multisample ( xs_parser_parsing_context_t* context ) {
    char token[xs_shader_parser_max_token_size_m];

    while ( context->head < context->eof ) {

        xs_parser_skip_to_text();
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        if ( std_str_cmp ( token, "samples" ) == 0 ) {
            xs_parser_skip_spaces();
            uint32_t u32;
            xs_parser_read_u32 ( context, u32 );
        } else if ( std_str_cmp ( token, "alpha_to_coverage" ) == 0 ) {
        } else if ( std_str_cmp ( token, "alpha_to_one" ) == 0 ) {
        } else if ( std_str_cmp ( token, "sample_shading" ) == 0 ) {
        } else {
            std_assert_m ( std_str_cmp ( token, "end" ) == 0 );
            break;
        }
    }
}
*/

/*
re: blend equqtion
    https://www.gamedev.net/forums/topic/573393-dx11-blending-questions/

    The blend equation goes something like this:
    srcColor = color output by pixel
    shaderdestColor = existing color in the render
    targetfinalColor = color that ultimately ends up in the render target after
        blendingfinalColor.rgb = (srcColor.rgb * SrcBlend) [BlendOp] (destColor.rgb * DestBlend);
        finalColor.a = (srcColor.a * SrcBlendAlpha) [BlendOpAlpha] (destColor.a * DestBlendAlpha);

    Your "standard" alpha-blending equation is actually like this:
    finalColor.rgb = (srcColor.rgb * srcColor.a) + (destColor.rgb * (1 - srcColor.a));finalColor.a = (srcColor.a * 1) + (destColor.a * 1);

    This basically gives you a lerp between the source color and the dest color based on the alpha value of the source alpha. So alpha = 1 gives you full source color, alpha = 0 gives you full dest color, and alpha = 0.5 gives you an equal mix. The states for this are as follows:
        BlendEnable = TRUE;
        BlendOp = D3D11_BLEND_OP_ADD;
        BlendOpAlpha = D3D11_BLEND_OP_ADD;
        SrcBlend = D3D11_BLEND_SRC_ALPHA;
        SrcBlendAlpha = D3D11_BLEND_ONE;
        DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        DestBlendAlpha = D3D11_BLEND_ONE;
*/

static void xs_parser_parse_render_target ( xs_parser_parsing_context_t* context ) {
    uint32_t rt_idx;
    {
        xs_parser_skip_spaces ( context );
        size_t len = xs_parser_read_u32 ( context, &rt_idx );
        std_assert_m ( len > 0 );
        std_assert_m ( rt_idx < xg_pipeline_output_max_color_targets_m );
    }

    xg_render_target_blend_state_t* blend_state = NULL;

    for ( size_t i = 0; i < context->graphics_pipeline->state.blend_state.render_targets_count; ++i ) {
        if ( rt_idx == context->graphics_pipeline->state.blend_state.render_targets[i].id ) {
            blend_state = &context->graphics_pipeline->state.blend_state.render_targets[i];
        }
    }

    if ( !blend_state ) {
        size_t idx = context->graphics_pipeline->state.blend_state.render_targets_count;
        context->graphics_pipeline->state.blend_state.render_targets[idx].id = rt_idx;
        blend_state = &context->graphics_pipeline->state.blend_state.render_targets[idx];
        ++context->graphics_pipeline->state.blend_state.render_targets_count;
    }

    xg_render_target_layout_t* layout = NULL;

    for ( size_t i = 0; i < context->graphics_pipeline->render_textures_layout.render_targets_count; ++i ) {
        if ( rt_idx == context->graphics_pipeline->render_textures_layout.render_targets[i].slot ) {
            layout = &context->graphics_pipeline->render_textures_layout.render_targets[i];
        }
    }

    if ( !layout ) {
        size_t idx = context->graphics_pipeline->render_textures_layout.render_targets_count;
        context->graphics_pipeline->render_textures_layout.render_targets[idx].slot = rt_idx;
        layout = &context->graphics_pipeline->render_textures_layout.render_targets[idx];
        ++context->graphics_pipeline->render_textures_layout.render_targets_count;
    }

    /*if ( context->result->state.blend_state.render_targets_count == xs_size_undefined_m ) {
        context->result->state.blend_state.render_targets_count = 0;
    }*/

    /*
    for ( size_t i = 0; i < context->result->state.blend_state.render_targets_count; ++i ) {
        if ( rt_idx == context->result->state.blend_state.render_targets[i].id ) {
            blend_state = &context->result->state.blend_state.render_targets[i];
        }
    }

    if ( !blend_state ) {
        size_t idx = context->result->state.blend_state.render_targets_count;
        context->result->state.blend_state.render_targets[idx].id = rt_idx;
        blend_state = &context->result->state.blend_state.render_targets[idx];
        ++context->result->state.blend_state.render_targets_count;
    }
    */

    char token[xs_shader_parser_max_token_size_m];

    while ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );

        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        if ( std_str_cmp ( token, "format" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            xg_format_e format = xs_parser_format_to_enum ( token );
            std_assert_m ( format != xg_format_undefined_m );
            layout->format = format;
        } else if ( std_str_cmp ( token, "samples_per_pixel" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            uint32_t spp;
            len = xs_parser_read_u32 ( context, &spp );
            std_assert_m ( len > 0 );
            std_assert_m ( spp == 1 || spp == 2 || spp == 4 || spp == 8 || spp == 16 || spp == 32 || spp == 64 );
            uint32_t index = std_bit_scan_32 ( spp );
            layout->samples_per_pixel = ( xg_sample_count_e ) index;
        } else if ( std_str_cmp ( token, "blend" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            blend_state->enable_blend = xs_parser_str_to_bool ( token );
        } else if ( std_str_cmp ( token, "blend_color_src" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            blend_state->color_src = xs_parser_blend_factor_to_enum ( token );
        } else if ( std_str_cmp ( token, "blend_color_dst" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            blend_state->color_dst = xs_parser_blend_factor_to_enum ( token );
        } else if ( std_str_cmp ( token, "blend_color_op" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            blend_state->color_op = xs_parser_blend_op_to_enum ( token );
        } else if ( std_str_cmp ( token, "blend_alpha_src" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            blend_state->alpha_src = xs_parser_blend_factor_to_enum ( token );
        } else if ( std_str_cmp ( token, "blend_alpha_dst" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            blend_state->alpha_dst = xs_parser_blend_factor_to_enum ( token );
        } else if ( std_str_cmp ( token, "blend_alpha_op" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            blend_state->alpha_op = xs_parser_blend_op_to_enum ( token );
        } else if ( std_str_cmp ( token, xs_parser_line_comment_token_m ) == 0 ) {
            xs_parser_skip_line ( context );
        } else {
            std_assert_m ( std_str_cmp ( token, "end" ) == 0 );
            break;
        }
    }
}

static void xs_parser_parse_depth_stencil ( xs_parser_parsing_context_t* context ) {
    char token[xs_shader_parser_max_token_size_m];
    bool depth_stencil_enabled = false;

    while ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        if ( std_str_cmp ( token, "min_depth_bound" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            float f32 = std_str_to_f32 ( token );
            //std_assert_m ( f32 >= 0 && f32 <= 1 );
            context->graphics_pipeline->state.depth_stencil_state.depth.min_bound = f32;
        } else if ( std_str_cmp ( token, "max_depth_bound" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            float f32 = std_str_to_f32 ( token );
            //std_assert_m ( f32 >= 0 && f32 <= 1 );
            context->graphics_pipeline->state.depth_stencil_state.depth.max_bound = f32;
        } else if ( std_str_cmp ( token, "depth_bound_test" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            context->graphics_pipeline->state.depth_stencil_state.depth.enable_bound_test = xs_parser_str_to_bool ( token );
        } else if ( std_str_cmp ( token, "depth_test" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            bool test_enabled = xs_parser_str_to_bool ( token );
            context->graphics_pipeline->state.depth_stencil_state.depth.enable_test = test_enabled;
            depth_stencil_enabled |= test_enabled;
        } else if ( std_str_cmp ( token, "depth_test_op" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            context->graphics_pipeline->state.depth_stencil_state.depth.compare_op = xs_parser_compare_op_to_enum ( token );
        } else if ( std_str_cmp ( token, "depth_write" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            bool write_enabled = xs_parser_str_to_bool ( token );
            context->graphics_pipeline->state.depth_stencil_state.depth.enable_write = write_enabled;
            depth_stencil_enabled |= write_enabled;
        } else if ( std_str_cmp ( token, "stencil_test" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            bool stencil_enabled = xs_parser_str_to_bool ( token );
            context->graphics_pipeline->state.depth_stencil_state.stencil.enable_test = stencil_enabled;
            depth_stencil_enabled |= stencil_enabled;
        } else if ( std_str_cmp ( token, "format" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            context->graphics_pipeline->render_textures_layout.depth_stencil.format = xs_parser_format_to_enum ( token );
        } else if ( std_str_cmp ( token, xs_parser_line_comment_token_m ) == 0 ) {
            xs_parser_skip_line ( context );
        } else {
            std_assert_m ( std_str_cmp ( token, "end" ) == 0 );
            break;
        }
    }

    context->graphics_pipeline->render_textures_layout.depth_stencil_enabled = depth_stencil_enabled;
}

static uint32_t xs_parser_parse_shader ( xs_parser_parsing_context_t* context, xg_shading_stage_e stage ) {
    char token[xs_shader_parser_max_token_size_m];

    uint32_t idx = -1;

    if ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        uint64_t hash = std_hash_block_64_m ( token, len );
        if ( std_hash_set_lookup ( &context->shader_references->set, hash ) ) {
            // TODO optional?
            return idx;
        } else {
            std_hash_set_insert ( &context->shader_references->set, hash );
        }

        idx = context->shader_references->count++;
        xs_parser_shader_reference_t* shader = &context->shader_references->array[idx];
        shader->stage = stage;
        std_str_copy_static_m ( shader->name, token );
    }

    return idx;
}

static void xs_parser_parse_constant ( xs_parser_parsing_context_t* context ) {
    char token[xs_shader_parser_max_token_size_m];

    struct {
        xg_shading_stage_bit_e stages;
        uint32_t size;
        uint32_t id;
    } push_constant;

    {
        xs_parser_skip_spaces ( context );
        size_t len = xs_parser_read_u32 ( context, &push_constant.id );
        std_assert_m ( len > 0 );
        std_assert_m ( push_constant.id < xg_pipeline_constant_max_bindings_m );
    }

    while ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        if ( std_str_cmp ( token, "stage" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            push_constant.stages |= xs_parser_shading_stage_to_bit ( token );
        } else if ( std_str_cmp ( token, "size" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_u32 ( context, &push_constant.size );
            std_assert_m ( len > 0 );
        } else {
            std_assert_m ( std_str_cmp ( token, "end" ) == 0 );
            break;
        }
    }

    xg_constant_binding_layout_t* binding_point = NULL;

    switch ( context->pipeline_type ) {
        case xg_pipeline_graphics_m:
            binding_point = &context->graphics_pipeline->constant_layout.binding_points[context->graphics_pipeline->constant_layout.binding_points_count++];
            break;

        case xg_pipeline_compute_m:
            binding_point = &context->compute_pipeline->constant_layout.binding_points[context->compute_pipeline->constant_layout.binding_points_count++];
            break;

        case xg_pipeline_raytrace_m:
            binding_point = &context->raytrace_pipeline->constant_layout.binding_points[context->raytrace_pipeline->constant_layout.binding_points_count++];
    }

    binding_point->stages = push_constant.stages;
    binding_point->size = push_constant.size;
    //binding_point->id = push_constant.id;
}

static void xs_parser_parse_define ( xs_parser_parsing_context_t* context ) {
    char token[xs_shader_parser_max_token_size_m];
    
    if ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );
        std_str_copy ( context->shader_definitions->array[context->shader_definitions->count].name, xs_shader_definition_name_max_len_m, token );

        xs_parser_skip_spaces ( context );
        int32_t value;
        len = xs_parser_read_i32 ( context, &value );
        std_assert_m ( len > 0 );
        context->shader_definitions->array[context->shader_definitions->count].value = value;

        context->shader_definitions->count += 1;
    }
}

typedef enum {
    xs_parser_buffer_type_uniform_m,
    xs_parser_buffer_type_storage_m,
} xs_parser_buffer_type_e;

typedef struct {
    xg_shading_stage_bit_e stages;
    xg_shader_binding_set_e set;
    xs_parser_buffer_type_e type;
    uint32_t shader_register;
} xs_parser_buffer_t;

static void xs_parser_add_buffer ( xs_parser_parsing_context_t* context, const xs_parser_buffer_t* buffer ) {
    std_assert_m ( buffer->stages != 0 );
    std_assert_m ( buffer->set < xg_shader_binding_set_count_m );

    xg_resource_binding_e type = xg_resource_binding_invalid_m;
    if ( buffer->type == xs_parser_buffer_type_uniform_m ) {
        type = xg_resource_binding_buffer_uniform_m;
    } else if ( buffer->type == xs_parser_buffer_type_storage_m ) {
        type = xg_resource_binding_buffer_storage_m;
    } else {
        std_assert_m ( false );
    }

    //xg_resource_binding_layout_t* binding_point = &context->resource_layouts[buffer->set].buffers[context->resource_layouts[buffer->set].buffer_count++];
    xg_resource_binding_layout_t* binding_point = &context->resource_layouts[buffer->set].resources[context->resource_layouts[buffer->set].resource_count++];
    binding_point->shader_register = buffer->shader_register;
    binding_point->type = type;
    binding_point->stages = buffer->stages;
}

static void xs_parser_parse_buffer ( xs_parser_parsing_context_t* context ) {
    char token[xs_shader_parser_max_token_size_m];

    xs_parser_buffer_t buffer = {
        .type = xs_parser_buffer_type_uniform_m,
        .stages = 0,
        .set = xg_shader_binding_set_dispatch_m
    };

    while ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        if ( std_str_cmp ( token, "update" ) == 0 || std_str_cmp ( token, "set" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len >  0 );
            buffer.set = xs_parser_shader_binding_set_to_enum ( token );
        } else if ( std_str_cmp ( token, "register" ) == 0 || std_str_cmp ( token, "binding" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_u32 ( context, &buffer.shader_register );
            std_assert_m ( len > 0 );
        } else if ( std_str_cmp ( token, "access" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            if ( std_str_cmp ( token, "read_write" ) == 0 || std_str_cmp ( token, "write" ) == 0 || std_str_cmp ( token, "storage" ) == 0 ) {
                buffer.type = xs_parser_buffer_type_storage_m;
            } else if ( std_str_cmp ( token, "uniform" ) == 0 ) {
                buffer.type = xs_parser_buffer_type_uniform_m;
            }
        } else if ( std_str_cmp ( token, "stage" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            buffer.stages |= xs_parser_shading_stage_to_bit ( token );
        } else if ( std_str_cmp ( token, xs_parser_line_comment_token_m ) == 0 ) {
            xs_parser_skip_line ( context );
        } else {
            std_assert_m ( std_str_cmp ( token, "end" ) == 0 );
            break;
        }
    }

    xs_parser_add_buffer ( context, &buffer );
}

typedef enum {
    xs_parser_texture_type_sampled_m,
    xs_parser_texture_type_storage_m,
} xs_parser_texture_type_e;

typedef struct {
    xg_shading_stage_bit_e stages;
    xg_shader_binding_set_e set;
    uint32_t shader_register;
    xs_parser_texture_type_e type;
} xs_parser_texture_t;

static void xs_parser_add_texture ( xs_parser_parsing_context_t* context, const xs_parser_texture_t* texture ) {
    std_assert_m ( texture->stages != 0 );
    std_assert_m ( texture->set < xg_shader_binding_set_count_m );

    xg_resource_binding_e type = xg_resource_binding_invalid_m;
    if ( texture->type == xs_parser_texture_type_sampled_m ) {
        type = xg_resource_binding_texture_to_sample_m;
    } else if ( texture->type == xs_parser_texture_type_storage_m ) {
        type = xg_resource_binding_texture_storage_m;
    } else {
        std_assert_m ( false );
    }

    //xg_resource_binding_layout_t* binding_point = &context->resource_layouts[texture->set].textures[context->resource_layouts[texture->set].texture_count++];
    xg_resource_binding_layout_t* binding_point = &context->resource_layouts[texture->set].resources[context->resource_layouts[texture->set].resource_count++];
    binding_point->shader_register = texture->shader_register;
    binding_point->type = type;
    binding_point->stages = texture->stages;
}

static void xs_parser_parse_texture ( xs_parser_parsing_context_t* context ) {
    char token[xs_shader_parser_max_token_size_m];

    xs_parser_texture_t texture = {
        .stages = 0,
        .set = xg_shader_binding_set_dispatch_m,
        .type = xs_parser_texture_type_sampled_m,
    };

    while ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        if ( std_str_cmp ( token, "update" ) == 0 || std_str_cmp ( token, "set" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len >  0 );
            texture.set = xs_parser_shader_binding_set_to_enum ( token );
        } else if ( std_str_cmp ( token, "register" ) == 0 || std_str_cmp ( token, "binding" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_u32 ( context, &texture.shader_register );
            std_assert_m ( len > 0 );
        } else if ( std_str_cmp ( token, "access" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );

            if ( std_str_cmp ( token, "read_write" ) == 0 || std_str_cmp ( token, "write" ) == 0 || std_str_cmp ( token, "storage" ) == 0 ) {
                texture.type = xs_parser_texture_type_storage_m;
            } else if ( std_str_cmp ( token, "sampled" ) == 0 ) {
                texture.type = xs_parser_texture_type_sampled_m;
            }
        } else if ( std_str_cmp ( token, "stage" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            texture.stages |= xs_parser_shading_stage_to_bit ( token );
        } else if ( std_str_cmp ( token, xs_parser_line_comment_token_m ) == 0 ) {
            xs_parser_skip_line ( context );
        } else {
            std_assert_m ( std_str_cmp ( token, "end" ) == 0 );
            break;
        }
    }

    xs_parser_add_texture ( context, &texture );
}

static void xs_parser_parse_ray_gen_shader ( xs_parser_parsing_context_t* context ) {
    uint32_t binding;
    {
        xs_parser_skip_spaces ( context );
        size_t len = xs_parser_read_u32 ( context, &binding );
        std_assert_m ( len > 0 );
        std_assert_m ( binding < xg_raytrace_shader_state_max_gen_shaders_m );
    }

    char token[xs_shader_parser_max_token_size_m];
    uint32_t shader_idx = -1;

    while ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        if ( std_str_cmp ( token, "shader" ) == 0 ) {
            shader_idx = xs_parser_parse_shader ( context, xg_shading_stage_ray_gen_m );
        } else {
            std_assert_m ( std_str_cmp ( token, "end" ) == 0 );
            break;
        }
    }

    std_assert_m ( shader_idx != -1 );
    uint32_t gen_idx = context->raytrace_pipeline->state.shader_state.gen_shader_count++;
    xg_raytrace_pipeline_gen_shader_t* gen_shader = &context->raytrace_pipeline->state.shader_state.gen_shaders[gen_idx];
    gen_shader->binding = binding;
    gen_shader->shader = shader_idx;
}

static void xs_parser_parse_ray_miss_shader ( xs_parser_parsing_context_t* context ) {
    uint32_t binding;
    {
        xs_parser_skip_spaces ( context );
        size_t len = xs_parser_read_u32 ( context, &binding );
        std_assert_m ( len > 0 );
        std_assert_m ( binding < xg_raytrace_shader_state_max_miss_shaders_m );
    }

    char token[xs_shader_parser_max_token_size_m];
    uint32_t shader_idx = -1;

    while ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        if ( std_str_cmp ( token, "shader" ) == 0 ) {
            shader_idx = xs_parser_parse_shader ( context, xg_shading_stage_ray_miss_m );
        } else {
            std_assert_m ( std_str_cmp ( token, "end" ) == 0 );
            break;
        }
    }

    std_assert_m ( shader_idx != -1 );
    uint32_t miss_idx = context->raytrace_pipeline->state.shader_state.miss_shader_count++;
    xg_raytrace_pipeline_miss_shader_t* miss_shader = &context->raytrace_pipeline->state.shader_state.miss_shaders[miss_idx];
    miss_shader->binding = binding;
    miss_shader->shader = shader_idx;
}

static void xs_parser_parse_ray_hit_shader_group ( xs_parser_parsing_context_t* context ) {
    uint32_t binding;
    {
        xs_parser_skip_spaces ( context );
        size_t len = xs_parser_read_u32 ( context, &binding );
        std_assert_m ( len > 0 );
        std_assert_m ( binding < xg_raytrace_shader_state_max_miss_shaders_m );
    }

    char token[xs_shader_parser_max_token_size_m];
    uint32_t closest_idx = -1;
    uint32_t any_idx = -1;
    uint32_t intersection_idx = -1;

    while ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        if ( std_str_cmp ( token, "hit_closest_shader" ) == 0 ) {
            closest_idx = xs_parser_parse_shader ( context, xg_shading_stage_ray_hit_closest_m );
        } else if ( std_str_cmp ( token, "hit_any_shader" ) == 0 ) {
            any_idx = xs_parser_parse_shader ( context, xg_shading_stage_ray_hit_any_m );
        } else if ( std_str_cmp ( token, "intersect_shader" ) == 0 ) {
            intersection_idx = xs_parser_parse_shader ( context, xg_shading_stage_ray_intersect_m );
        } else {
            std_assert_m ( std_str_cmp ( token, "end" ) == 0 );
            break;
        }
    }

    std_assert_m ( closest_idx != -1 );
    uint32_t group_idx = context->raytrace_pipeline->state.shader_state.hit_group_count++;
    xg_raytrace_pipeline_hit_shader_group_t* hit_group = &context->raytrace_pipeline->state.shader_state.hit_groups[group_idx];
    hit_group->binding = binding;
    hit_group->closest_shader = closest_idx;
    hit_group->any_shader = any_idx;
    hit_group->intersection_shader = intersection_idx;
}

typedef struct {
    xg_shading_stage_bit_e stages;
    xg_shader_binding_set_e set;
    uint32_t shader_register;
} xs_parser_ray_world_t;

static void xs_parser_add_ray_world ( xs_parser_parsing_context_t* context, const xs_parser_ray_world_t* world ) {
    std_assert_m ( world->stages != 0 );
    std_assert_m ( world->set < xg_shader_binding_set_count_m );
    std_assert_m ( world->shader_register != UINT32_MAX );
    std_assert_m ( context->pipeline_type == xg_pipeline_raytrace_m );

    xg_resource_binding_layout_t* binding_point = &context->resource_layouts[world->set].resources[context->resource_layouts[world->set].resource_count++];
    binding_point->shader_register = world->shader_register;
    binding_point->type = xg_resource_binding_raytrace_world_m;
    binding_point->stages = world->stages;
}

static void xs_parser_parse_ray_world ( xs_parser_parsing_context_t* context ) {
    char token[xs_shader_parser_max_token_size_m];
    xs_parser_ray_world_t ray_world;

    while ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        if ( std_str_cmp ( token, "update" ) == 0 || std_str_cmp ( token, "set" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            ray_world.set = xs_parser_shader_binding_set_to_enum ( token );
        } else if ( std_str_cmp ( token, "register" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_u32 ( context, &ray_world.shader_register );
            std_assert_m ( len > 0 );
        } else if ( std_str_cmp ( token, "stage" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            ray_world.stages |= xs_parser_shading_stage_to_bit ( token );
        } else {
            std_assert_m ( std_str_cmp ( token, "end" ) == 0 );
            break;
        }
    }

    xs_parser_add_ray_world ( context, &ray_world );
}

typedef struct {
    xg_shading_stage_bit_e stages;
    xg_shader_binding_set_e set;
    uint32_t shader_register;
} xs_parser_sampler_t;

static void xs_parser_add_sampler ( xs_parser_parsing_context_t* context, const xs_parser_sampler_t* sampler ) {
    std_assert_m ( sampler->stages != 0 );
    std_assert_m ( sampler->set < xg_shader_binding_set_count_m );
    std_assert_m ( sampler->shader_register != UINT32_MAX );

    //xg_resource_binding_layout_t* binding_point = &context->resource_layouts[sampler->set].samplers[context->resource_layouts[sampler->set].sampler_count++];
    xg_resource_binding_layout_t* binding_point = &context->resource_layouts[sampler->set].resources[context->resource_layouts[sampler->set].resource_count++];
    binding_point->shader_register = sampler->shader_register;
    binding_point->type = xg_resource_binding_sampler_m;
    binding_point->stages = sampler->stages;
}

static void xs_parser_parse_sampler ( xs_parser_parsing_context_t* context ) {
    char token[xs_shader_parser_max_token_size_m];

    xs_parser_sampler_t sampler;

    sampler.stages = 0;
    sampler.set = xg_shader_binding_set_dispatch_m;//xg_shader_binding_set_invalid_m;
    sampler.shader_register = UINT32_MAX;

    while ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        if ( std_str_cmp ( token, "update" ) == 0 || std_str_cmp ( token, "set" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            sampler.set = xs_parser_shader_binding_set_to_enum ( token );
        } else if ( std_str_cmp ( token, "register" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_u32 ( context, &sampler.shader_register );
            std_assert_m ( len > 0 );
        } else if ( std_str_cmp ( token, "stage" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            sampler.stages |= xs_parser_shading_stage_to_bit ( token );
        } else {
            std_assert_m ( std_str_cmp ( token, "end" ) == 0 );
            break;
        }
    }

    xs_parser_add_sampler ( context, &sampler );
}

static size_t xs_parser_extract_token_array ( char* token, size_t len ) {
    if ( token[len - 1] != ']' ) {
        return 1;
    }
    
    size_t i = std_str_find_reverse ( token, len - 1, "[" );
    std_assert_m ( i != std_str_find_null_m );

    token[len - 1] = '\0';
    uint32_t array_size = std_str_to_u32 ( token + i + 1 );
    token[i] = '\0';
    return array_size;
}

/*
Format:
    begin bindings
        <buffer/texture/sampler/ray_world> <optional:[n]> <optional: uniform/sampled/storage/vertex/fragment/compute/...>
    end bindings
*/
static void xs_parser_parse_bindings ( xs_parser_parsing_context_t* context ) {
    char token[xs_shader_parser_max_token_size_m];

    uint32_t binding_register = 0;
    xg_shader_binding_set_e binding_set = xg_shader_binding_set_dispatch_m;

    xs_parser_skip_spaces ( context );
    size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
    if ( len > 0 ) {
        binding_set = xs_parser_shader_binding_set_to_enum ( token );
    }

    while ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        // TODO instead of always defaulting to default_shading_stage try to parse an optional additional stage somewhere?
        if ( std_str_starts_with ( token , "buffer" ) ) {
            xs_parser_buffer_t buffer = {
                .set = binding_set,
                .type = xs_parser_buffer_type_uniform_m,
            };

            // try to parse array size
            uint32_t array_size = 1;
            if ( token[len - 1] == ']' ) {
                array_size = xs_parser_extract_token_array ( token, len );
                len = std_str_len ( token );
            } else {
                xs_parser_skip_spaces ( context );
                if ( *xs_parser_peek ( context ) == '[' ) {
                    len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
                    array_size = xs_parser_extract_token_array ( token, len );
                }
            }

            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );

            while ( len > 0) {
                if ( std_str_cmp ( token, "uniform" ) == 0 ) { 
                    buffer.type = xs_parser_buffer_type_uniform_m;
                } else if ( std_str_cmp ( token, "storage" ) == 0 ) {
                    buffer.type = xs_parser_buffer_type_storage_m;
                } else {
                    xg_shading_stage_bit_e stage = xs_parser_shading_stage_to_bit ( token );
                    std_assert_m ( stage != xg_shading_stage_bit_none_m );
                    buffer.stages |= stage;
                }

                xs_parser_skip_spaces ( context );
                len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            }

            if ( buffer.stages == xg_shading_stage_bit_none_m ) {
                buffer.stages = xs_parser_default_shading_stage ( context );
            }

            for ( uint32_t i = 0; i < array_size; ++i ) {
                buffer.shader_register = binding_register++;
                xs_parser_add_buffer ( context, &buffer );
            }
        } else if ( std_str_starts_with ( token, "texture" ) ) {
            xs_parser_texture_t texture = {
                .set = binding_set,
                .type = xs_parser_texture_type_sampled_m,
            };

            // try to parse array size
            uint32_t array_size = 1;
            if ( token[len - 1] == ']' ) {
                array_size = xs_parser_extract_token_array ( token, len );
                len = std_str_len ( token );
            } else {
                xs_parser_skip_spaces ( context );
                if ( *xs_parser_peek ( context ) == '[' ) {
                    len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
                    array_size = xs_parser_extract_token_array ( token, len );
                }
            }

            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );

            while ( len > 0 ) {
                if ( std_str_cmp ( token, "sampled" ) == 0 ) { 
                    texture.type = xs_parser_texture_type_sampled_m;
                } else if ( std_str_cmp ( token, "storage" ) == 0 ) {
                    texture.type = xs_parser_texture_type_storage_m;
                } else {
                    xg_shading_stage_bit_e stage = xs_parser_shading_stage_to_bit ( token );
                std_assert_m ( stage != xg_shading_stage_bit_none_m );
                    texture.stages |= stage;
                }

                xs_parser_skip_spaces ( context );
                len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            }

            if ( texture.stages == xg_shading_stage_bit_none_m ) {
                texture.stages = xs_parser_default_shading_stage ( context );
            }

            for ( uint32_t i = 0; i < array_size; ++i ) {
                texture.shader_register = binding_register++;
                xs_parser_add_texture ( context, &texture );
            }
        } else if ( std_str_starts_with ( token, "sampler" ) ) {
            xs_parser_sampler_t sampler = {
                .set = binding_set,
            };

            // try to parse array size
            uint32_t array_size = 1;
            if ( token[len - 1] == ']' ) {
                array_size = xs_parser_extract_token_array ( token, len );
                len = std_str_len ( token );
            } else {
                xs_parser_skip_spaces ( context );
                if ( *xs_parser_peek ( context ) == '[' ) {
                    len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
                    array_size = xs_parser_extract_token_array ( token, len );
                }
            }

            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );

            while ( len > 0 ) {
                if ( token[len - 1] == ']' ) {
                    array_size = xs_parser_extract_token_array ( token, len );
                    len = std_str_len ( token );
                } else {
                    xg_shading_stage_bit_e stage = xs_parser_shading_stage_to_bit ( token );
                    std_assert_m ( stage != xg_shading_stage_bit_none_m );
                    sampler.stages |= stage;
                }

                xs_parser_skip_spaces ( context );
                len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            }

            if ( sampler.stages == xg_shading_stage_bit_none_m ) {
                sampler.stages = xs_parser_default_shading_stage ( context );
            }

            for ( uint32_t i = 0; i < array_size; ++i ) {
                sampler.shader_register = binding_register++;
                xs_parser_add_sampler ( context, &sampler );
            }
        } else if ( std_str_starts_with ( token, "ray_world" ) ) {
            xs_parser_ray_world_t world = {
                .set = binding_set,
            };

            // try to parse array size
            uint32_t array_size = 1;
            if ( token[len - 1] == ']' ) {
                array_size = xs_parser_extract_token_array ( token, len );
                len = std_str_len ( token );
            } else {
                xs_parser_skip_spaces ( context );
                if ( *xs_parser_peek ( context ) == '[' ) {
                    len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
                    array_size = xs_parser_extract_token_array ( token, len );
                }
            }

            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );

            while ( len > 0 ) {
                if ( token[len - 1] == ']' ) {
                    array_size = xs_parser_extract_token_array ( token, len );
                    len = std_str_len ( token );
                } else {
                    xg_shading_stage_bit_e stage = xs_parser_shading_stage_to_bit ( token );
                    std_assert_m ( stage != xg_shading_stage_bit_none_m );
                    world.stages |= stage;
                }

                xs_parser_skip_spaces ( context );
                len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            }

            if ( world.stages == xg_shading_stage_bit_none_m ) {
                world.stages = xs_parser_default_shading_stage ( context );
            }

            for ( uint32_t i = 0; i < array_size; ++i ) {
                world.shader_register = binding_register++;
                xs_parser_add_ray_world ( context, &world );
            }
        } else {
            std_assert_m ( std_str_cmp ( token, "end" ) == 0 );
            break;
        }
    }
}

#if 0
static bool xs_parser_parse_if ( xs_parser_parsing_context_t* context ) {
    char token[xs_shader_parser_max_token_size_m];

    if ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len );
        uint64_t hash = xs_hash_string_m ( token, len );
        return std_hash_set_lookup ( &context->defines, hash );
    }

    return false;
}

static size_t xs_parser_skip_if_branch ( xs_parser_parsing_context_t* context, bool* enter_else ) {
    const char* begin = context->head;

    while ( context->head < context->eof ) {
        xs_parser_skip_line ( context );
        xs_parser_skip_to_text ( context );

        if ( std_str_starts_with ( context->head, "endif" ) ) {
            context->head += 5;
            *enter_else = false;
            break;
        } else if ( std_str_starts_with ( context->head, "else" ) ) {
            context->head += 4;
            *enter_else = true;
            break;
        }
    }

    return ( size_t ) ( context->head - begin );
}
#endif

static void xs_parser_parsing_context_init ( xs_parser_parsing_context_t* context, xg_pipeline_e pipeline_type, void* state, std_buffer_t buffer, const char* path ) {
    context->pipeline_type = pipeline_type;
    context->path = path;
    context->state = state;

    if ( pipeline_type == xg_pipeline_graphics_m ) {
        std_auto_m graphics_state = ( xs_parser_graphics_pipeline_state_t* ) state;
        context->graphics_pipeline = &graphics_state->params;
        context->shader_references = &graphics_state->shader_references;
        context->shader_definitions = &graphics_state->shader_definitions;
        context->resource_layouts = graphics_state->resource_layouts;
    } else if ( pipeline_type == xg_pipeline_compute_m ) {
        std_auto_m compute_state = ( xs_parser_compute_pipeline_state_t* ) state;
        context->compute_pipeline = &compute_state->params;
        context->shader_references = &compute_state->shader_references;
        context->shader_definitions = &compute_state->shader_definitions;
        context->resource_layouts = compute_state->resource_layouts;
    } else if ( pipeline_type == xg_pipeline_raytrace_m ) {
        std_auto_m raytrace_state = ( xs_parser_raytrace_pipeline_state_t* ) state;
        context->raytrace_pipeline = &raytrace_state->params;
        context->shader_references = &raytrace_state->shader_references;
        context->shader_definitions = &raytrace_state->shader_definitions;
        context->resource_layouts = raytrace_state->resource_layouts;
    } else {
        std_assert_m ( false );
    }

    context->shader_references->set = std_static_hash_set_m ( context->shader_references->set_array );

    context->begin = ( const char* ) buffer.base;
    context->eof = ( const char* ) ( buffer.base + buffer.size );
    context->head = context->begin;
    context->line_count = 0;
    {
        size_t result = std_str_find_reverse ( context->begin, 0, "\n" );

        if ( result == std_str_find_null_m ) {
            context->line_begin = context->begin;
        } else {
            context->line_begin = context->begin + result;
        }
    }
}

static void xs_parser_parse_include ( xs_parser_parsing_context_t* context ) {
    char token[xs_shader_parser_max_token_size_m];

    if ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        char path[std_path_size_m];
        std_str_copy ( path, std_path_size_m, context->path );
        len = std_path_pop ( path );
        std_path_append ( path, std_path_size_m - len, token );

        if ( context->pipeline_type == xg_pipeline_graphics_m ) {
            std_auto_m state = ( xs_parser_graphics_pipeline_state_t* ) context->state;
            xs_parser_parse_graphics_pipeline_state_from_path ( state, path );
        } else if ( context->pipeline_type == xg_pipeline_compute_m ) {
            std_auto_m state = ( xs_parser_compute_pipeline_state_t* ) context->state;
            xs_parser_parse_compute_pipeline_state_from_path ( state, path );
        } else if ( context->pipeline_type == xg_pipeline_raytrace_m ) {
            std_auto_m state = ( xs_parser_raytrace_pipeline_state_t* ) context->state;
            xs_parser_parse_raytrace_pipeline_state_from_path ( state, path );
        } else {
            std_assert_m ( false );
        }

    }
}

static void xs_parser_log_bad_token ( xs_parser_parsing_context_t* context, const char* token ) {
    std_log_error_m ( "Bad token " std_fmt_str_m " while parsing " std_fmt_str_m " line " std_fmt_u32_m, token, context->path, context->line_count );
}

static void xs_parser_parse_graphics_pipeline_state ( xs_parser_parsing_context_t* context ) {
    char token[xs_shader_parser_max_token_size_m];

    while ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );

        if ( context->head == context->eof ) {
            break;
        }

        if ( std_str_cmp ( token, "begin" ) == 0 ) {
            {
                xs_parser_skip_to_text ( context );
                size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
                std_assert_m ( len > 0 );
            }

            // TODO: resources, includes, define conditionals
            if ( std_str_cmp ( token, "input" ) == 0 ) {
                xs_parser_parse_input_desc ( context );
            } else if ( std_str_cmp ( token, "viewport" ) == 0 ) {
                xs_parser_parse_viewport ( context );
            } else if ( std_str_cmp ( token, "scissor" ) == 0 ) {
                xs_parser_parse_scissor ( context );
            } else if ( std_str_cmp ( token, "rasterizer" ) == 0 ) {
                xs_parser_parse_rasterizer ( context );
            } else if ( std_str_cmp ( token, "multisample" ) == 0 ) {
                std_not_implemented_m(); // TODO
            } else if ( std_str_cmp ( token, "render_target" ) == 0 ) {
                xs_parser_parse_render_target ( context );
            } else if ( std_str_cmp ( token, "depth_stencil" ) == 0 ) {
                xs_parser_parse_depth_stencil ( context );
            } else if ( std_str_cmp ( token, "buffer" ) == 0 ) {
                xs_parser_parse_buffer ( context );
            } else if ( std_str_cmp ( token, "texture" ) == 0 ) {
                xs_parser_parse_texture ( context );
            } else if ( std_str_cmp ( token, "sampler" ) == 0 ) {
                xs_parser_parse_sampler ( context );
            } else if ( std_str_cmp ( token, "constant" ) == 0 ) {
                xs_parser_parse_constant ( context );
            } else if ( std_str_cmp ( token, "bindings" ) == 0 ) {
                xs_parser_parse_bindings ( context );
            } else {
                xs_parser_log_bad_token ( context, token );
            }
        } else if ( std_str_cmp ( token, "vertex_shader" ) == 0 ) {
            xs_parser_parse_shader ( context, xg_shading_stage_vertex_m );
        } else if ( std_str_cmp ( token, "fragment_shader" ) == 0 ) {
            xs_parser_parse_shader ( context, xg_shading_stage_fragment_m );
#if 0
        } else if ( std_str_cmp ( token, "if" ) == 0 ) {
            // TODO
            bool enter = xs_parser_parse_if ( &context );

            if ( enter ) {
                ++branch_depth;
            } else {
                xs_parser_skip_if_branch ( &context, &enter );

                if ( enter ) {
                    ++branch_depth;
                }
            }
        } else if ( std_str_cmp ( token, "endif" ) == 0 ) {
            std_assert_m ( branch_depth > 0 );
            --branch_depth;
#endif
        } else if ( std_str_cmp ( token, "include" ) == 0 ) {
            xs_parser_parse_include ( context );
        } else if ( std_str_cmp ( token, "define" ) == 0 ) {
            xs_parser_parse_define ( context );
        } else if ( std_str_starts_with ( token, xs_parser_line_comment_token_m ) ) {
            xs_parser_skip_line ( context );
        } else if ( std_str_starts_with ( token, xs_parser_multiline_comment_begin_token_m ) ) {
            xs_parser_skip_comment_block ( context );
        } else if ( std_str_cmp ( token, "end" ) == 0 ) {
            return; // possible end of variation block
        } else {
            xs_parser_log_bad_token ( context, token );
        }
    }
}

bool xs_parser_parse_graphics_pipeline_state_from_path ( xs_parser_graphics_pipeline_state_t* state, const char* path ) {
    std_buffer_t file_buffer = std_file_read_to_virtual_heap ( path );

    xs_parser_parsing_context_t context;
    xs_parser_parsing_context_init ( &context, xg_pipeline_graphics_m, state, file_buffer, path );
    xs_parser_parse_graphics_pipeline_state ( &context );

    std_virtual_heap_free ( file_buffer.base );
    return true;
}

static void xs_parser_parse_raytrace_pipeline_state ( xs_parser_parsing_context_t* context ) {
    char token[xs_shader_parser_max_token_size_m];

    while ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );

        // TODO avoid this?
        if ( context->head == context->eof ) {
            break;
        }

        if ( std_str_cmp ( token, "begin" ) == 0 ) {
            {
                xs_parser_skip_to_text ( context );
                size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
                std_assert_m ( len > 0 );
            }

            if ( std_str_cmp ( token, "buffer" ) == 0 ) {
                xs_parser_parse_buffer ( context );
            } else if ( std_str_cmp ( token, "texture" ) == 0 ) {
                xs_parser_parse_texture ( context );
            } else if ( std_str_cmp ( token, "sampler" ) == 0 ) {
                xs_parser_parse_sampler ( context );
            } else if ( std_str_cmp ( token, "constant" ) == 0 ) {
                xs_parser_parse_constant ( context );
            } else if ( std_str_cmp ( token, "bindings" ) == 0 ) {
                xs_parser_parse_bindings ( context );
            } else if ( std_str_cmp ( token, "ray_world" ) == 0 ) {
                xs_parser_parse_ray_world ( context );
            } else if ( std_str_cmp ( token, "ray_gen" ) == 0 ) {
                xs_parser_parse_ray_gen_shader ( context );
            } else if ( std_str_cmp ( token, "ray_miss" ) == 0 ) {
                xs_parser_parse_ray_miss_shader ( context );
            } else if ( std_str_cmp ( token, "ray_hit" ) == 0 ) {
                xs_parser_parse_ray_hit_shader_group ( context );
            } else {
                xs_parser_log_bad_token ( context, token );
            }
        } else if ( std_str_cmp ( token, "include" ) == 0 ) {
            xs_parser_parse_include ( context );
        } else if ( std_str_cmp ( token, "define" ) == 0 ) {
            xs_parser_parse_define ( context );
        } else if ( std_str_starts_with ( token, xs_parser_line_comment_token_m ) ) {
            xs_parser_skip_line ( context );
        } else if ( std_str_starts_with ( token, xs_parser_multiline_comment_begin_token_m ) ) {
            xs_parser_skip_comment_block ( context );
        } else {
            xs_parser_log_bad_token ( context, token );
        }
    }
}

static void xs_parser_parse_compute_pipeline_state ( xs_parser_parsing_context_t* context ) {
    char token[xs_shader_parser_max_token_size_m];

    while ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );

        if ( context->head == context->eof ) {
            break;
        }

        if ( std_str_cmp ( token, "begin" ) == 0 ) {
            {
                xs_parser_skip_to_text ( context );
                size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
                std_assert_m ( len > 0 );
            }

            if ( std_str_cmp ( token, "buffer" ) == 0 ) {
                xs_parser_parse_buffer ( context );
            } else if ( std_str_cmp ( token, "texture" ) == 0 ) {
                xs_parser_parse_texture ( context );
            } else if ( std_str_cmp ( token, "sampler" ) == 0 ) {
                xs_parser_parse_sampler ( context );
            } else if ( std_str_cmp ( token, "constant" ) == 0 ) {
                xs_parser_parse_constant ( context );
            } else if ( std_str_cmp ( token, "bindings" ) == 0 ) {
                xs_parser_parse_bindings ( context );
            } else {
                xs_parser_log_bad_token ( context, token );
            }
        } else if ( std_str_cmp ( token, "compute_shader" ) == 0 ) {
            xs_parser_parse_shader ( context, xg_shading_stage_compute_m );
#if 0
        } else if ( std_str_cmp ( token, "if" ) == 0 ) {
            // TODO
            bool enter = xs_parser_parse_if ( &context );

            if ( enter ) {
                ++branch_depth;
            } else {
                xs_parser_skip_if_branch ( &context, &enter );

                if ( enter ) {
                    ++branch_depth;
                }
            }
        } else if ( std_str_cmp ( token, "endif" ) == 0 ) {
            std_assert_m ( branch_depth > 0 );
            --branch_depth;
#endif
        } else if ( std_str_cmp ( token, "include" ) == 0 ) {
            xs_parser_parse_include ( context );
        } else if ( std_str_cmp ( token, "define" ) == 0 ) {
            xs_parser_parse_define ( context );
        } else if ( std_str_starts_with ( token, xs_parser_line_comment_token_m ) ) {
            xs_parser_skip_line ( context );
        } else if ( std_str_starts_with ( token, xs_parser_multiline_comment_begin_token_m ) ) {
            xs_parser_skip_comment_block ( context );
        } else {
            xs_parser_log_bad_token ( context, token );
        }
    }
}

bool xs_parser_parse_compute_pipeline_state_from_path ( xs_parser_compute_pipeline_state_t* state, const char* path ) {
    std_buffer_t file_buffer = std_file_read_to_virtual_heap ( path );

    xs_parser_parsing_context_t context;
    xs_parser_parsing_context_init ( &context, xg_pipeline_compute_m, state, file_buffer, path );
    xs_parser_parse_compute_pipeline_state ( &context );

    std_virtual_heap_free ( file_buffer.base );
    return true;
}

bool xs_parser_parse_raytrace_pipeline_state_from_path ( xs_parser_raytrace_pipeline_state_t* state, const char* path ) {
    std_buffer_t file_buffer = std_file_read_to_virtual_heap ( path );

    xs_parser_parsing_context_t context;
    xs_parser_parsing_context_init ( &context, xg_pipeline_raytrace_m, state, file_buffer, path );
    xs_parser_parse_raytrace_pipeline_state ( &context );

    std_virtual_heap_free ( file_buffer.base );
    return true;
}
