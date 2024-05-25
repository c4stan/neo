#include "xs_parser.h"

#include <std_hash.h>
#include <std_log.h>

#include "xs_database.h"

#include <fs.h>

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
    };

    xs_parser_shader_references_t* shader_references;
    xs_parser_shader_permutations_t* shader_permutations;
#endif
} xs_parser_parsing_context_t;

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

    std_assert_m ( *context->head == ' ' || *context->head == '\n' );

    *out = result * sign;

    return ( size_t ) ( context->head - begin );
}
std_warnings_restore_state_m()

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

    if ( std_str_cmp ( format, "R16_FLOAT" ) == 0 ) {
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

static xg_resource_binding_set_e xs_parser_resource_binding_set_to_enum ( const char* set ) {
    if ( std_str_cmp ( set, "per_frame" ) == 0 || std_str_cmp ( set, "frame" ) == 0 ) {
        return xg_resource_binding_set_per_frame_m;
    } else if ( std_str_cmp ( set, "per_view" ) == 0 || std_str_cmp ( set, "view" ) == 0 ) {
        return xg_resource_binding_set_per_view_m;
    } else if ( std_str_cmp ( set, "per_material" ) == 0 || std_str_cmp ( set, "material" ) == 0 ) {
        return xg_resource_binding_set_per_material_m;
    } else if ( std_str_cmp ( set, "per_draw" ) == 0 || std_str_cmp ( set, "draw" ) == 0 ) {
        return xg_resource_binding_set_per_draw_m;
    }

    std_assert_m ( false );
    return xg_resource_binding_set_per_draw_m;
}

static xg_shading_stage_bit_e xs_parser_shading_stage_to_bit ( const char* stage ) {
    if ( std_str_cmp ( stage, "vertex" ) == 0 ) {
        return xg_shading_stage_bit_vertex_m;
    } else if ( std_str_cmp ( stage, "fragment" ) == 0 || std_str_cmp ( stage, "pixel" ) == 0 ) {
        return xg_shading_stage_bit_fragment_m;
    } else if ( std_str_cmp ( stage, "compute" ) == 0 ) {
        return xg_shading_stage_bit_compute_m;
    }

    std_assert_m ( false );
    return 0;
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
            context->graphics_pipeline->state.dynamic_state.enabled_states[xg_graphics_pipeline_dynamic_state_viewport_m] = xs_parser_str_to_bool ( token );
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
            context->graphics_pipeline->state.dynamic_state.enabled_states[xg_graphics_pipeline_dynamic_state_scissor_m] = xs_parser_str_to_bool ( token );
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

    for ( size_t i = 0; i < context->graphics_pipeline->render_textures.render_targets_count; ++i ) {
        if ( rt_idx == context->graphics_pipeline->render_textures.render_targets[i].slot ) {
            layout = &context->graphics_pipeline->render_textures.render_targets[i];
        }
    }

    if ( !layout ) {
        size_t idx = context->graphics_pipeline->render_textures.render_targets_count;
        context->graphics_pipeline->render_textures.render_targets[idx].slot = rt_idx;
        layout = &context->graphics_pipeline->render_textures.render_targets[idx];
        ++context->graphics_pipeline->render_textures.render_targets_count;
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
            context->graphics_pipeline->render_textures.depth_stencil.format = xs_parser_format_to_enum ( token );
        } else if ( std_str_cmp ( token, xs_parser_line_comment_token_m ) == 0 ) {
            xs_parser_skip_line ( context );
        } else {
            std_assert_m ( std_str_cmp ( token, "end" ) == 0 );
            break;
        }
    }

    context->graphics_pipeline->render_textures.depth_stencil_enabled = depth_stencil_enabled;
}

static void xs_parser_parse_shader ( xs_parser_parsing_context_t* context, xg_shading_stage_e stage ) {
    char token[xs_shader_parser_max_token_size_m];

    if ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        std_str_copy ( context->shader_references->shaders[stage], xs_shader_name_max_len_m, token );
        context->shader_references->referenced_stages |= xg_shading_stage_enum_to_bit_m ( stage );
    }
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
            binding_point = &context->graphics_pipeline->constant_bindings.binding_points[context->graphics_pipeline->constant_bindings.binding_points_count++];
            break;

        case xg_pipeline_compute_m:
            binding_point = &context->compute_pipeline->constant_bindings.binding_points[context->compute_pipeline->constant_bindings.binding_points_count++];
            break;
    }

    binding_point->stages = push_constant.stages;
    binding_point->size = push_constant.size;
    binding_point->id = push_constant.id;
}

static void xs_parser_parse_buffer ( xs_parser_parsing_context_t* context ) {
    uint32_t shader_register = 0;
    {
        //xs_parser_skip_spaces ( context );
        //size_t len =
        //xs_parser_read_u32 ( context, &shader_register );
        //std_assert_m ( len > 0 );
    }

    char token[xs_shader_parser_max_token_size_m];

    struct {
        xg_shading_stage_bit_e stages;
        xg_resource_binding_set_e update_set;
        bool is_texel_buffer;
        bool write_access;
    } buffer;

    buffer.write_access = false;
    buffer.is_texel_buffer = false;
    buffer.stages = 0;
    buffer.update_set = xg_resource_binding_set_per_draw_m;//xg_resource_binding_set_invalid_m;

    while ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        if ( std_str_cmp ( token, "update" ) == 0 || std_str_cmp ( token, "set" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len >  0 );
            buffer.update_set = xs_parser_resource_binding_set_to_enum ( token );
        } else if ( std_str_cmp ( token, "register" ) == 0 || std_str_cmp ( token, "binding" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_u32 ( context, &shader_register );
            std_assert_m ( len > 0 );
        } else if ( std_str_cmp ( token, "access" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );

            if ( std_str_cmp ( token, "read_write" ) == 0 || std_str_cmp ( token, "write" ) == 0 ) {
                buffer.write_access = true;
            } else {
                std_assert_m ( std_str_cmp ( token, "read" ) == 0 );
            }
        } else if ( std_str_cmp ( token, "stage" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            buffer.stages |= xs_parser_shading_stage_to_bit ( token );
        } else if ( std_str_cmp ( token, "texel" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            buffer.is_texel_buffer = xs_parser_str_to_bool ( token );
        } else if ( std_str_cmp ( token, xs_parser_line_comment_token_m ) == 0 ) {
            xs_parser_skip_line ( context );
        } else {
            std_assert_m ( std_str_cmp ( token, "end" ) == 0 );
            break;
        }
    }

    std_assert_m ( buffer.stages != 0 );
    std_assert_m ( buffer.update_set != xg_resource_binding_set_invalid_m );

    xg_resource_binding_e type;

    if ( buffer.is_texel_buffer ) {
        if ( buffer.write_access ) {
            type = xg_resource_binding_buffer_texel_storage_m;
        } else {
            type = xg_resource_binding_buffer_texel_uniform_m;
        }
    } else {
        if ( buffer.write_access ) {
            type = xg_resource_binding_buffer_storage_m;
        } else {
            type = xg_resource_binding_buffer_uniform_m;
        }
    }

    /*if ( context->result->resource_bindings.binding_points_count == xs_size_undefined_m ) {
        context->result->resource_bindings.binding_points_count = 0;
    }*/

    xg_resource_binding_layout_t* binding_point = NULL;

    switch ( context->pipeline_type ) {
        case xg_pipeline_graphics_m:
            for ( size_t i = 0; i < context->graphics_pipeline->resource_bindings.binding_points_count; ++i ) {
                if ( context->graphics_pipeline->resource_bindings.binding_points[i].type == type ) { // TODO is this proper in glsl?
                    std_assert_m ( context->graphics_pipeline->resource_bindings.binding_points[i].shader_register != shader_register
                        || context->graphics_pipeline->resource_bindings.binding_points[i].set != buffer.update_set );
                }
            }

            binding_point = &context->graphics_pipeline->resource_bindings.binding_points[context->graphics_pipeline->resource_bindings.binding_points_count++];

            break;

        case xg_pipeline_compute_m:
            for ( size_t i = 0; i < context->compute_pipeline->resource_bindings.binding_points_count; ++i ) {
                if ( context->compute_pipeline->resource_bindings.binding_points[i].type == type ) { // TODO is this proper in glsl?
                    std_assert_m ( context->compute_pipeline->resource_bindings.binding_points[i].shader_register != shader_register
                        || context->compute_pipeline->resource_bindings.binding_points[i].set != buffer.update_set );
                }
            }

            binding_point = &context->compute_pipeline->resource_bindings.binding_points[context->compute_pipeline->resource_bindings.binding_points_count++];

            break;
    }

    binding_point->shader_register = shader_register;
    binding_point->type = type;
    binding_point->stages = buffer.stages;
    binding_point->set = buffer.update_set;
}

static void xs_parser_parse_texture ( xs_parser_parsing_context_t* context ) {
    uint32_t shader_register = 0;
    {
        //xs_parser_skip_spaces ( context );
        //size_t len = xs_parser_read_u32 ( context, &shader_register );
        //std_assert_m ( len > 0 );
    }

    char token[xs_shader_parser_max_token_size_m];

    struct {
        xg_shading_stage_bit_e stages;
        xg_resource_binding_set_e update_set;
        bool write_access;
    } texture;

    texture.write_access = false;
    texture.stages = 0;
    texture.update_set = xg_resource_binding_set_per_draw_m;//xg_resource_binding_set_invalid_m;

    while ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        if ( std_str_cmp ( token, "update" ) == 0 || std_str_cmp ( token, "set" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len >  0 );
            texture.update_set = xs_parser_resource_binding_set_to_enum ( token );
        } else if ( std_str_cmp ( token, "register" ) == 0 || std_str_cmp ( token, "binding" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_u32 ( context, &shader_register );
            std_assert_m ( len > 0 );
        } else if ( std_str_cmp ( token, "access" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );

            if ( std_str_cmp ( token, "read_write" ) == 0 || std_str_cmp ( token, "write" ) == 0 ) {
                texture.write_access = true;
            } else {
                std_assert_m ( std_str_cmp ( token, "read" ) == 0 );
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

    std_assert_m ( texture.stages != 0 );
    std_assert_m ( texture.update_set != xg_resource_binding_set_invalid_m );

    xg_resource_binding_e type;

    if ( texture.write_access ) {
        type = xg_resource_binding_texture_storage_m;
    } else {
        type = xg_resource_binding_texture_to_sample_m;
    }

    /*if ( context->result->resource_bindings.binding_points_count == xs_size_undefined_m ) {
        context->result->resource_bindings.binding_points_count = 0;
    }*/

    //for ( size_t i = 0; i < context->result->resource_bindings.binding_points_count; ++i ) {
    //    if ( context->result->resource_bindings.binding_points[i].type == type ) {
    //        std_assert_m ( context->result->resource_bindings.binding_points[i].shader_register != shader_register );
    //    }
    //}

    xg_resource_binding_layout_t* binding_point = NULL;

    switch ( context->pipeline_type ) {
        case xg_pipeline_graphics_m:
            for ( size_t i = 0; i < context->graphics_pipeline->resource_bindings.binding_points_count; ++i ) {
                if ( context->graphics_pipeline->resource_bindings.binding_points[i].type == type ) { // TODO is this proper in glsl?
                    std_assert_m ( context->graphics_pipeline->resource_bindings.binding_points[i].shader_register != shader_register
                        || context->graphics_pipeline->resource_bindings.binding_points[i].set != texture.update_set );
                }
            }

            binding_point = &context->graphics_pipeline->resource_bindings.binding_points[context->graphics_pipeline->resource_bindings.binding_points_count++];

            break;

        case xg_pipeline_compute_m:
            for ( size_t i = 0; i < context->compute_pipeline->resource_bindings.binding_points_count; ++i ) {
                if ( context->compute_pipeline->resource_bindings.binding_points[i].type == type ) { // TODO is this proper in glsl?
                    std_assert_m ( context->compute_pipeline->resource_bindings.binding_points[i].shader_register != shader_register
                        || context->compute_pipeline->resource_bindings.binding_points[i].set != texture.update_set );
                }
            }

            binding_point = &context->compute_pipeline->resource_bindings.binding_points[context->compute_pipeline->resource_bindings.binding_points_count++];

            break;
    }

    binding_point->shader_register = shader_register;
    binding_point->type = type;
    binding_point->stages = texture.stages;
    binding_point->set = texture.update_set;
}

static void xs_parser_parse_sampler ( xs_parser_parsing_context_t* context ) {
    char token[xs_shader_parser_max_token_size_m];

    struct {
        xg_shading_stage_bit_e stages;
        xg_resource_binding_set_e update_set;
        uint32_t shader_register;
    } sampler;

    sampler.stages = 0;
    sampler.update_set = xg_resource_binding_set_per_draw_m;//xg_resource_binding_set_invalid_m;
    sampler.shader_register = UINT32_MAX;

    while ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        if ( std_str_cmp ( token, "update" ) == 0 || std_str_cmp ( token, "set" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            sampler.update_set = xs_parser_resource_binding_set_to_enum ( token );
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

    std_assert_m ( sampler.stages != 0 );
    std_assert_m ( sampler.update_set != xg_resource_binding_set_invalid_m );
    std_assert_m ( sampler.shader_register != UINT32_MAX );

    xg_resource_binding_layout_t* binding_point = NULL;

    switch ( context->pipeline_type ) {
        case xg_pipeline_graphics_m:
            binding_point = &context->graphics_pipeline->resource_bindings.binding_points[context->graphics_pipeline->resource_bindings.binding_points_count++];
            break;

        case xg_pipeline_compute_m:
            binding_point = &context->compute_pipeline->resource_bindings.binding_points[context->compute_pipeline->resource_bindings.binding_points_count++];
            break;
    }

    binding_point->shader_register = sampler.shader_register;
    binding_point->type = xg_resource_binding_sampler_m;
    binding_point->stages = sampler.stages;
    binding_point->set = sampler.update_set;
}

static void xs_parser_parse_permutation ( xs_parser_parsing_context_t* context ) {
#if 1
    char token[xs_shader_parser_max_token_size_m];

    xs_parser_shader_permutation_t* permutation = &context->shader_permutations->permutations[context->shader_permutations->permutation_count++];
    permutation->name[0] = 0;
    permutation->value_count = 0;
    permutation->stages = 0;

    while ( context->head < context->eof ) {
        xs_parser_skip_to_text ( context );
        size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
        std_assert_m ( len > 0 );

        if ( std_str_cmp ( token, "name" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, permutation->name, xs_shader_permutation_name_max_len_m );
            std_assert_m ( len > 0 );
        } else if ( std_str_cmp ( token, "count" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_u32 ( context, &permutation->value_count );
            std_assert_m ( len > 0 );
        } else if ( std_str_cmp ( token, "stage" ) == 0 ) {
            xs_parser_skip_spaces ( context );
            len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
            std_assert_m ( len > 0 );
            permutation->stages |= xs_parser_shading_stage_to_bit ( token );
        } else {
            std_assert_m ( std_str_cmp ( token, "end" ) == 0 );
            break;
        }
    }

    std_assert_m ( permutation->name[0] != 0 );
    std_assert_m ( permutation->value_count != 0 );
    std_assert_m ( permutation->stages != 0 );

#else
    xs_parser_shader_permutation_t* permutation = &context->shader_permutations->permutations[context->shader_permutations->permutation_count++];

    char token[xs_shader_parser_max_token_size_m];
    xs_parser_skip_spaces ( context );
    size_t len = xs_parser_read_word ( context, token, xs_shader_parser_max_token_size_m );
    std_assert_m ( len < xs_shader_permutation_name_max_len_m && len > 0 );
    std_str_copy_m ( permutation->name, token );

    xs_parser_skip_spaces ( context );
    uint32_t bit_start;
    uint32_t bit_count;
    len = xs_parser_read_u32 ( context, &bit_start );
    std_assert_m ( len > 0 );
    len = xs_parser_read_u32 ( context, &bit_count );
    std_assert_m ( len > 0 );
    permutation->bit_start = bit_start;
    permutation->bit_count = bit_count;
#endif
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
        context->shader_permutations = &graphics_state->shader_permutations;
    } else if ( pipeline_type == xg_pipeline_compute_m ) {
        std_auto_m compute_state = ( xs_parser_compute_pipeline_state_t* ) state;
        context->compute_pipeline = &compute_state->params;
        context->shader_references = &compute_state->shader_references;
        context->shader_permutations = &compute_state->shader_permutations;
    } else {
        std_assert_m ( false );
    }


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

        char path[fs_path_size_m];
        fs_i* fs = std_module_get_m ( fs_module_name_m );
        std_str_copy ( path, fs_path_size_m, context->path );
        len = fs->pop_path ( path );
        fs->append_path ( path, fs_path_size_m - len, token );

        if ( context->pipeline_type == xg_pipeline_graphics_m ) {
            std_auto_m state = ( xs_parser_graphics_pipeline_state_t* ) context->state;
            xs_parser_parse_graphics_pipeline_state_from_path ( state, path );
        } else if ( context->pipeline_type == xg_pipeline_compute_m ) {
            std_auto_m state = ( xs_parser_compute_pipeline_state_t* ) context->state;
            xs_parser_parse_compute_pipeline_state_from_path ( state, path );
        } else {
            std_assert_m ( false );
        }

    }
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
            } else if ( std_str_cmp ( token, "permutation" ) == 0 ) {
                xs_parser_parse_permutation ( context );
            } else if ( std_str_cmp ( token, "variation" ) == 0 ) {
                xs_parser_skip_block ( context );
            } else {
                std_log_error_m ( "Unknown token" );
            }
        } else if ( std_str_cmp ( token, "permutation" ) == 0 ) {
            xs_parser_parse_permutation ( context );
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
        } else if ( std_str_starts_with ( token, xs_parser_line_comment_token_m ) ) {
            xs_parser_skip_line ( context );
        } else if ( std_str_starts_with ( token, xs_parser_multiline_comment_begin_token_m ) ) {
            xs_parser_skip_comment_block ( context );
        } else if ( std_str_cmp ( token, "end" ) == 0 ) {
            return; // possible end of variation block
        } else {
            std_log_error_m ( "Unknown token" );
        }
    }
}

static void xs_parser_parse_variation ( xs_parser_parsing_context_t* context, const void* base, void* variation ) {
    xg_pipeline_e pipeline_type = context->pipeline_type;

    if ( pipeline_type == xg_pipeline_graphics_m ) {
        std_auto_m graphics_base = ( const xs_parser_graphics_pipeline_state_t* ) base;
        std_auto_m graphics_variation = ( xs_parser_graphics_pipeline_state_t* ) variation;

        *graphics_variation = *graphics_base;

        context->graphics_pipeline = &graphics_variation->params;
        context->shader_references = &graphics_variation->shader_references;
        context->shader_permutations = &graphics_variation->shader_permutations;

        xs_parser_parse_graphics_pipeline_state ( context );
    } else {
        std_assert_m ( false );
    }
}

uint32_t xs_parser_parse_graphics_pipeline_variations_from_path ( xs_parser_graphics_pipeline_state_t* variations, const xs_parser_graphics_pipeline_state_t* base_state, const char* path ) {
    fs_i* fs = std_module_get_m ( fs_module_name_m );

#if 0
    fs_file_h pipeline_state_file = fs->open_file ( path, fs_file_read_m );
    std_assert_m ( pipeline_state_file != fs_null_handle_m );
    fs_file_info_t pipeline_state_file_info;
    std_verify_m ( fs->get_file_info ( &pipeline_state_file_info, pipeline_state_file ) );
    void* file_buffer = std_virtual_heap_alloc ( pipeline_state_file_info.size, 16 );
    std_verify_m ( fs->read_file ( state_alloc, pipeline_state_file_info.size, pipeline_state_file ) );
    fs->close_file ( pipeline_state_file );
#else
    std_buffer_t file_buffer = fs->read_file_path_to_heap ( path );
#endif

    xs_parser_parsing_context_t context;
    xs_parser_parsing_context_init ( &context, xg_pipeline_graphics_m, ( void* ) base_state, file_buffer, path );

    char token[xs_shader_parser_max_token_size_m];
    //size_t branch_depth = 0;

    uint32_t variation_count = 0;

    while ( context.head < context.eof ) {
        xs_parser_skip_to_text ( &context );
        xs_parser_read_word ( &context, token, xs_shader_parser_max_token_size_m );

        if ( context.head == context.eof ) {
            break;
        }

        if ( std_str_cmp ( token, "begin" ) == 0 ) {
            {
                xs_parser_skip_to_text ( &context );
                size_t len = xs_parser_read_word ( &context, token, xs_shader_parser_max_token_size_m );
                std_assert_m ( len > 0 );
            }

            if ( std_str_cmp ( token, "variation" ) == 0 ) {
                std_assert_m ( variation_count < xs_shader_max_variations_m );
                xs_parser_parse_variation ( &context, base_state, &variations[variation_count++] );
            } else {
                xs_parser_skip_block ( &context );
            }
        } else {
            xs_parser_skip_line ( &context );
        }
    }

    std_virtual_heap_free ( file_buffer.base );

    // TODO remove permutations
    //if ( context.shader_permutations->permutation_count == 0 ) {
    //    context.shader_permutations->permutation_count = 1;
    //    context.shader_permutations->permutations[0].define_count = 0;
    //}

    return variation_count;
}

bool xs_parser_parse_graphics_pipeline_state_from_path ( xs_parser_graphics_pipeline_state_t* state, const char* path ) {
    fs_i* fs = std_module_get_m ( fs_module_name_m );

#if 0
    fs_file_h pipeline_state_file = fs->open_file ( path, fs_file_read_m );
    std_assert_m ( pipeline_state_file != fs_null_handle_m );
    fs_file_info_t pipeline_state_file_info;
    std_verify_m ( fs->get_file_info ( &pipeline_state_file_info, pipeline_state_file ) );
    void* file_buffer = std_virtual_heap_alloc ( pipeline_state_file_info.size, 16 );
    std_verify_m ( fs->read_file ( NULL, state_alloc.buffer, pipeline_state_file ) );
    fs->close_file ( pipeline_state_file );
#else
    std_buffer_t file_buffer = fs->read_file_path_to_heap ( path );
#endif

    //state_alloc.buffer.base[pipeline_state_file_info.size] = 0;

    xs_parser_parsing_context_t context;
    xs_parser_parsing_context_init ( &context, xg_pipeline_graphics_m, state, file_buffer, path );

    xs_parser_parse_graphics_pipeline_state ( &context );

    std_virtual_heap_free ( file_buffer.base );

    // TODO remove permutations
    //if ( context.shader_permutations->permutation_count == 0 ) {
    //    context.shader_permutations->permutation_count = 1;
    //    context.shader_permutations->permutations[0].define_count = 0;
    //}

    return true;
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
            } else {
                std_log_error_m ( "Unknown token" );
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
        } else if ( std_str_starts_with ( token, xs_parser_line_comment_token_m ) ) {
            xs_parser_skip_line ( context );
        } else if ( std_str_starts_with ( token, xs_parser_multiline_comment_begin_token_m ) ) {
            xs_parser_skip_comment_block ( context );
        } else {
            std_log_error_m ( "Unknown token" );
        }
    }
}

bool xs_parser_parse_compute_pipeline_state_from_path ( xs_parser_compute_pipeline_state_t* state, const char* path ) {
    fs_i* fs = std_module_get_m ( fs_module_name_m );

#if 0
    fs_file_h pipeline_state_file = fs->open_file ( path, fs_file_read_m );
    std_assert_m ( pipeline_state_file != fs_null_handle_m );
    fs_file_info_t pipeline_state_file_info;
    std_verify_m ( fs->get_file_info ( &pipeline_state_file_info, pipeline_state_file ) );
    std_alloc_t state_alloc = std_virtual_heap_alloc ( pipeline_state_file_info.size, 16 );
    std_verify_m ( fs->read_file ( NULL, state_alloc.buffer, pipeline_state_file ) );
    fs->close_file ( pipeline_state_file );
#else
    std_buffer_t file_buffer = fs->read_file_path_to_heap ( path );
#endif

    xs_parser_parsing_context_t context;
    xs_parser_parsing_context_init ( &context, xg_pipeline_compute_m, state, file_buffer, path );

    xs_parser_parse_compute_pipeline_state ( &context );

    std_virtual_heap_free ( file_buffer.base );

    //if ( context.shader_permutations->permutation_count == 0 ) {
    //    context.shader_permutations->permutation_count = 1;
    //    context.shader_permutations->permutations[0].define_count = 0;
    //}

    return true;
}
