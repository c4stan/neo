#include "xui_element.h"

#include "xui_workload.h"
#include "xui_font.h"

#include <std_log.h>

std_warnings_ignore_m ( "-Wunused-function" )

static xui_element_state_t* xui_element_state;

void xui_element_load ( xui_element_state_t* state ) {
    xui_element_state = state;

    std_mem_zero_m ( xui_element_state );
}

void xui_element_reload ( xui_element_state_t* state ) {
    xui_element_state = state;
}

void xui_element_unload ( void ) {
}

static bool xui_element_cursor_test ( int64_t x, int64_t y, int64_t width, int64_t height ) {
    int64_t cursor_x = xui_element_state->input_state.cursor_x;
    int64_t cursor_y = xui_element_state->input_state.cursor_y;

    if ( cursor_x < x || cursor_x > x + width || cursor_y < y || cursor_y > y + height ) {
        return false;
    }

    return true;
}

static bool xui_element_layer_cursor_test ( int64_t x, int64_t y, int64_t width, int64_t height ) {
    xui_element_layer_t* layer = &xui_element_state->layers[xui_element_state->layer_count - 1];
    x += layer->x;
    y += layer->y;

    return xui_element_cursor_test ( x, y, width, height );
}

static bool xui_element_cursor_click() {
    return xui_element_state->mouse_down && xui_element_state->mouse_down_tick == xui_element_state->current_tick;
}

// x,y coords are expected to be relative to the parent layer
// TODO rename to _push
static xui_element_layer_t* xui_element_layer_add ( uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t padding_x, uint32_t padding_y, const xui_style_t* style ) {
    // if there's a previous layer take that into account
    if ( xui_element_state->layer_count > 0 ) {
        xui_element_layer_t* layer = &xui_element_state->layers[xui_element_state->layer_count - 1];

        // clamp width and height
        if ( x + width > layer->width ) {
            width = layer->width - x;
        }

        if ( y + height > layer->height ) {
            height = layer->height - y;
        }

        // offset the new layer's global origin by parent's origin
        x += layer->x; // + frame->line_offset;
        y += layer->y; //  + frame->line_y;
    }

    xui_element_layer_t* layer = &xui_element_state->layers[xui_element_state->layer_count++];
    std_assert_m ( xui_element_state->layer_count <= xui_element_max_layers_m );

    layer->x = x;
    layer->y = y;
    layer->width = width;
    layer->height = height;
    layer->line_height = 0;
    layer->line_offset = 0;
    layer->line_offset_rev = 0;
    layer->line_y = 0;
    layer->line_padding_x = padding_x;
    layer->line_padding_y = padding_y;
    layer->style = *style;

    if ( xui_element_cursor_test ( x, y, width, height ) && xui_element_cursor_click() ) {
        xui_element_state->active_layer = xui_element_state->layer_count;
    }

    return layer;
}

static void xui_element_layer_pop ( void ) {
    xui_element_state->layer_count--;
}

static void xui_element_layer_pop_all ( void ) {
    xui_element_state->layer_count = 0;
}

// try to make space for a new element in a layer.
// elements get appended horizontally on the current line, from left to right
static bool xui_element_layer_add_element ( uint32_t width, uint32_t height, const xui_style_t* style ) {
    xui_element_layer_t* layer = &xui_element_state->layers[xui_element_state->layer_count - 1];

    // apply line padding
    width += layer->line_padding_x * 2;
    height += layer->line_padding_y * 2;

    // check for enough space
    if ( width + layer->line_offset + layer->line_offset_rev > layer->width ) {
        return false;
    }

    if ( height + layer->line_y > layer->height ) {
        return false;
    }

    // update line height
    if ( height > layer->line_height ) {
        layer->line_height = height;
    }

    // update line offset
    if ( style->horizontal_alignment == xui_horizontal_alignment_left_to_right_m ) {
        layer->line_offset += width;
    } else if ( style->horizontal_alignment == xui_horizontal_alignment_right_to_left_m ) {
        layer->line_offset_rev += width;
    } else {
        std_not_implemented_m();
    }

    return true;
}

static bool xui_element_acquire_active ( uint64_t id, uint32_t sub_id ) {
    if ( xui_element_state->active_id == 0 && !xui_element_state->cleared_active ) {
        xui_element_state->active_id = id;
        xui_element_state->active_sub_id = sub_id;
        return true;
    }

    return false;
}

static bool xui_element_acquire_hovered ( uint64_t id, uint32_t sub_id ) {
    // same as per active
    if ( xui_element_state->hovered_id == 0 && !xui_element_state->cleared_hovered ) {
        xui_element_state->hovered_id = id;
        xui_element_state->hovered_sub_id = sub_id;
        return true;
    }

    return false;
}

static bool xui_element_release_active ( uint64_t id ) {
    if ( xui_element_state->active_id == id ) {
        xui_element_state->active_id = 0;
        xui_element_state->active_sub_id = 0;
        xui_element_state->cleared_active = true;

        return true;
    }

    return false;
}

static bool xui_element_release_hovered ( uint64_t id ) {
    if ( xui_element_state->hovered_id == id ) {
        xui_element_state->hovered_id = 0;
        xui_element_state->hovered_sub_id = 0;
        xui_element_state->cleared_hovered = true;

        return true;
    }

    return false;
}

static void xui_element_draw_rect ( xui_workload_h workload, xui_color_t color, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint64_t sort_order ) {
    xui_draw_rect_t rect = xui_default_draw_rect_m;
    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;
    rect.color = color;
    rect.sort_order = sort_order;
    xui_workload_cmd_draw ( workload, &rect, 1 );
}

static void xui_element_draw_tri ( xui_workload_h workload, xui_color_t color, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint64_t sort_order ) {
    xui_draw_tri_t tri = xui_default_draw_tri_m;
    tri.xy0[0] = x0;
    tri.xy0[1] = y0;
    tri.xy1[0] = x1;
    tri.xy1[1] = y1;
    tri.xy2[0] = x2;
    tri.xy2[1] = y2;
    tri.color = color;
    tri.sort_order = sort_order;
    xui_workload_cmd_draw_tri ( workload, &tri, 1 );
}

static uint32_t xui_element_draw_string ( xui_workload_h workload, xui_font_h font, const char* text, uint32_t x, uint32_t y, uint32_t height, uint64_t sort_order ) {
    size_t len = std_str_len ( text );
    // TODO assert on len

    xui_font_info_t font_info;
    xui_font_get_info ( &font_info, font );
    float scale = 1;//( float ) height / font_info.pixel_height;

    uint32_t x_offset = 0;
    float fx = x;
    float fy = y + font_info.pixel_height;

    for ( uint32_t i = 0; i < len; ++i ) {
        xui_font_char_box_t box = xui_font_char_box_get ( &fx, &fy, font, text[i] );

        xui_draw_rect_t rect = xui_default_draw_rect_m;
        //rect.x = x + x_offset;
        //rect.y = y + ( font_info.pixel_height - box.height ) * scale;
        //rect.width = box.width * scale;
        //rect.height = box.height * scale;
        rect.x = box.xy0[0];
        rect.y = box.xy0[1];
        rect.width = box.xy1[0] - box.xy0[0];
        rect.height = box.xy1[1] - box.xy0[1];
        rect.color = xui_color_white_m;
        rect.texture = xui_font_atlas_get ( font );
        rect.linear_sampler_filter = false;
        rect.uv0[0] = box.uv0[0];
        rect.uv0[1] = box.uv0[1];
        rect.uv1[0] = box.uv1[0];
        rect.uv1[1] = box.uv1[1];
        rect.sort_order = sort_order;

        x_offset += box.width * scale;

        xui_workload_cmd_draw ( workload, &rect, 1 ); // TODO batch? change the api to only take 1?
    }

    return fx - x;
}

// TODO return height too
static uint32_t xui_element_string_width ( const char* text, xui_font_h font, uint32_t pixel_height ) {
    size_t len = std_str_len ( text );
    // TODO assert on len

    xui_font_info_t font_info;
    xui_font_get_info ( &font_info, font );
    float scale = 1;//( float ) pixel_height / font_info.pixel_height;

    uint32_t width = 0;
    float fx = 0;
    float fy = 0;

    for ( uint32_t i = 0; i < len; ++i ) {
        xui_font_char_box_t box = xui_font_char_box_get ( &fx, &fy, font, text[i] );
        width += box.width * scale;
    }

    return ( uint32_t ) fx;
}

void xui_element_update ( const wm_window_info_t* window_info, const wm_input_state_t* input_state ) {
    //xui_element_state->base_x = -window_info->x - window_info->border_left;
    //xui_element_state->base_y = -window_info->y - window_info->border_top;
    //xui_element_state->base_width = window_info->width;
    //xui_element_state->base_height = window_info->height;
    xui_element_state->os_window_width = window_info->width;
    xui_element_state->os_window_height = window_info->height;

    xui_element_state->current_tick = std_tick_now();

    xui_element_state->mouse_delta_x = input_state->cursor_x - xui_element_state->input_state.cursor_x;
    xui_element_state->mouse_delta_y = input_state->cursor_y - xui_element_state->input_state.cursor_y;

    if ( input_state->mouse[wm_mouse_state_left_m] && !xui_element_state->mouse_down ) {
        xui_element_state->mouse_down_tick = xui_element_state->current_tick;
    }

    xui_element_state->mouse_down = input_state->mouse[wm_mouse_state_left_m];

    xui_element_state->input_state = *input_state;

    xui_element_state->hovered_id = 0;

    if ( !xui_element_state->mouse_down ) {
        xui_element_state->active_layer = 0;
    }

    xui_element_state->cleared_hovered = false;
    xui_element_state->cleared_active = false;
}

void xui_element_update_end ( void ) {
    // TODO remove?
}

xui_style_t xui_element_inherit_style ( const xui_style_t* style ) {
    if (xui_element_state->layer_count == 0) {
        return *style;
    }

    xui_style_t* parent = &xui_element_state->layers[xui_element_state->layer_count - 1].style;
    xui_style_t result;

    result.font = style->font != xui_null_handle_m ? style->font : parent->font;
    result.font_height = style->font_height != 0 ? style->font_height : parent->font_height;
    result.color = style->color.u32 != 0 ? style->color : parent->color;
    result.font_color = style->font_color.u32 != 0 ? style->font_color : parent->font_color;
    result.horizontal_alignment = style->horizontal_alignment != xui_horizontal_alignment_invalid_m ? style->horizontal_alignment : parent->horizontal_alignment;

    return result;
}

void xui_element_window_begin ( xui_workload_h workload, xui_window_state_t* state ) {
    xui_style_t style = xui_element_inherit_style ( &state->style );

    uint32_t title_pad_x = 10;
    uint32_t title_pad_y = 5;
    uint32_t border_height = style.font_height + title_pad_y * 2;

    xui_element_layer_t* layer = xui_element_layer_add ( state->x, state->y, state->width, state->height, state->padding_x, state->padding_y, &style );
    layer->line_y = border_height;

    // drag
    if ( xui_element_layer_cursor_test ( 0, 0, layer->width, border_height ) ) {
        xui_element_acquire_hovered ( state->id, 0 );

        if ( xui_element_cursor_click() ) {
            xui_element_acquire_active ( state->id, 0 );
        }
    } else {
        xui_element_release_hovered ( state->id );
    }

    if ( xui_element_state->active_id == state->id && xui_element_state->active_sub_id == 0 ) {
        //float delta = std_tick_to_milli_f32 ( xui_element_state.current_tick - xui_element_state.mouse_down_tick );

        //if ( delta > 100.f ) {
        state->x += xui_element_state->mouse_delta_x;
        state->y += xui_element_state->mouse_delta_y;
        //}

        if ( state->x < 0 ) {
            state->x = 0;
        }

        if ( state->x + state->width > xui_element_state->os_window_width ) {
            state->x = xui_element_state->os_window_width - state->width;
        }

        if ( state->y < 0 ) {
            state->y = 0;
        }

        if ( state->y + state->height > xui_element_state->os_window_height ) {
            state->y = xui_element_state->os_window_height - state->height;
        }
    }

    // resize
    if ( xui_element_layer_cursor_test ( layer->width - 20, layer->height - 20, 20, 20 ) ) {
        if ( xui_element_state->mouse_down && xui_element_state->mouse_down_tick == xui_element_state->current_tick ) {
            xui_element_acquire_active ( state->id, 1 );
        }
    }

    uint32_t corner_size = 15;

    if ( xui_element_state->active_id == state->id && xui_element_state->active_sub_id == 1 ) {
        //float delta = std_tick_to_milli_f32 ( xui_element_state.current_tick - xui_element_state.mouse_down_tick );

        //if ( delta > 100.f ) {
        //if ( xui_element_state->input_state.cursor_x + xui_element_state->base_x > layer->x + title_pad_x || state->width > title_pad_x + 10 ) {
        int32_t width = state->width;
        int32_t height = state->height;

        int32_t min_width = title_pad_x + 10;
        int32_t min_height = border_height + corner_size;

        if ( width > min_width ) {
            width += xui_element_state->mouse_delta_x;
        } else {
            if ( xui_element_state->input_state.cursor_x > state->x + min_width ) {
                uint32_t delta = xui_element_state->input_state.cursor_x - ( state->x + min_width );
                width += delta;
            }
        }

        if ( height > min_height ) {
            height += xui_element_state->mouse_delta_y;
        } else {
            if ( xui_element_state->input_state.cursor_y > state->y + min_height ) {
                uint32_t delta = xui_element_state->input_state.cursor_y - ( state->y + min_height );
                height += delta;
            }
        }

        //if ( width > min_width || xui_element_state->input_state.cursor_x > state->x + min_width ) {
        //    width += xui_element_state->mouse_delta_x;
        //}

        //if ( height > min_height || xui_element_state->input_state.cursor_y > state->y + min_height ) {
        //    height += xui_element_state->mouse_delta_y;
        //}

        //}

        //if ( xui_element_state->input_state.cursor_y + xui_element_state->base_y > layer->y + border_height || state->height > border_height ) {
        //}

        if ( state->x + width > xui_element_state->os_window_width ) {
            width = xui_element_state->os_window_width - state->x;
        }

        if ( state->y + height > xui_element_state->os_window_height ) {
            height = xui_element_state->os_window_height - state->y;
        }

        if ( height < border_height + corner_size ) {
            height = border_height + corner_size;
        }

        if ( width < ( int32_t ) title_pad_x + 10 ) {
            width = title_pad_x + 10;
        }

        state->width = width;
        state->height = height;

        //}
    }

    if ( xui_element_state->active_id == state->id && !xui_element_state->mouse_down ) {
        xui_element_release_active ( state->id );
    }

    // draw
    //  base background
    xui_element_draw_rect ( workload, style.color, layer->x, layer->y, layer->width, layer->height, state->sort_order );

    //  title bar
    xui_element_draw_rect ( workload, xui_color_rgb_mul_m ( style.color, 0.3 ), layer->x, layer->y, layer->width, style.font_height + title_pad_y * 2, state->sort_order );

    //  title text
    xui_element_draw_string ( workload, style.font, state->title, layer->x + title_pad_x, layer->y + title_pad_y / 2, style.font_height, state->sort_order );

#if 0
    // title tabs
    for ( uint32_t i = 0; i < state->tab_count; ++i ) {
        uint32_t width = xui_element_string_width ( state->tabs[i], style->font, style->font_height );

        if ( state->tab_count > 1 ) {
            // tab hover
            if ( xui_element_layer_cursor_test ( x_offset, 0, width + title_pad_x * 2, style->font_height + title_pad_y * 2 ) ) {
                xui_element_state->hovered_id = state->id;
                xui_element_state->hovered_sub_id = 2 + i;

                xui_element_draw_rect ( workload, xui_color_rgb_mul_m ( style->color, 0 ), layer->x + x_offset, layer->y, width + title_pad_x * 2, style->font_height + title_pad_y * 2, state->sort_order );

                if ( xui_element_state->mouse_down && xui_element_state->mouse_down_tick == xui_element_state->current_tick ) {
                    xui_element_state->active_id = state->id;
                    xui_element_state->active_sub_id = 2 + i;
                }
            }
        }

        // tab name
        // TODO use labels and xui_element_layer_add_element?
        x_offset += title_pad_x;
        x_offset += xui_element_draw_string ( workload, style->font, state->tabs[i], layer->x + x_offset, layer->y + title_pad_y, style->font_height, state->sort_order );
        x_offset += title_pad_x;
    }

    // tab idx update
    if ( !xui_element_state->mouse_down ) {
        if ( xui_element_state->active_id == state->id && xui_element_state->active_sub_id >= 2 ) {
            if ( xui_element_state->hovered_id == state->id && xui_element_state->hovered_sub_id == xui_element_state->active_sub_id ) {
                state->tab_idx = xui_element_state->active_sub_id - 2;
            }
        }
    }

#endif

    //  resize corner
    xui_element_draw_tri ( workload, xui_color_rgb_mul_m ( style.color, 0.5 ),
        layer->x + layer->width, layer->y + layer->height - corner_size,
        layer->x + layer->width, layer->y + layer->height,
        layer->x + layer->width - corner_size, layer->y + layer->height,
        state->sort_order  );
}

void xui_element_window_end ( xui_workload_h workload ) {
    std_unused_m ( workload );
    xui_element_layer_pop_all();
}

void xui_element_section_begin ( xui_workload_h workload, xui_section_state_t* state ) {
    xui_style_t style = xui_element_inherit_style ( &state->style );

    uint32_t title_pad_x = 10;
    uint32_t title_pad_y = 4;
    uint32_t border_height = style.font_height + title_pad_y * 2;

    xui_element_layer_t* layer = &xui_element_state->layers[xui_element_state->layer_count - 1];
    uint32_t x = layer->line_offset;
    uint32_t y = layer->line_y;

    uint32_t header_height = style.font_height + title_pad_y * 2;

    xui_element_layer_add_element ( layer->width - layer->line_offset - layer->line_offset_rev - layer->line_padding_x * 2, header_height, &style );
    xui_element_newline();

    // state
    if ( xui_element_layer_cursor_test ( x, y, layer->width, header_height ) ) {
        xui_element_acquire_hovered ( state->id, 0 );

        if ( xui_element_cursor_click() ) {
            xui_element_acquire_active ( state->id, 0 );
        }
    } else {
        xui_element_release_hovered ( state->id );
    }

    bool pressed = false;

    if ( !xui_element_state->mouse_down && xui_element_state->active_id == state->id ) {
        if ( xui_element_state->hovered_id == state->id ) {
            pressed = true;
        }

        xui_element_release_active ( state->id );
    }

    if ( pressed ) {
        state->minimized = !state->minimized;
    }

    // draw
    //  header bar
    xui_element_draw_rect ( workload, xui_color_rgb_mul_m ( style.color, 0.6 ), layer->x + x, layer->y + y, layer->width, header_height, state->sort_order );

    if ( !state->minimized ) {
        xui_element_draw_tri ( workload, xui_color_rgb_mul_m ( style.color, 1 ), 
            layer->x + x + title_pad_x,                          layer->y + y + title_pad_y, 
            layer->x + x + title_pad_x + style.font_height,     layer->y + y + title_pad_y, 
            layer->x + x + title_pad_x + style.font_height / 2, layer->y + y + title_pad_y + style.font_height, 
            state->sort_order );
    } else {
        xui_element_draw_tri ( workload, xui_color_rgb_mul_m ( style.color, 1 ), 
            layer->x + x + title_pad_x,                     layer->y + y + title_pad_y, 
            layer->x + x + title_pad_x + style.font_height, layer->y + y + title_pad_y + style.font_height / 2, 
            layer->x + x + title_pad_x,                     layer->y + y + title_pad_y + style.font_height, 
            state->sort_order );
    }

    //  title text
    xui_element_draw_string ( workload, style.font, state->title, layer->x + x + title_pad_x * 2 + style.font_height, layer->y + y + title_pad_y / 2, style.font_height, state->sort_order );

    xui_element_layer_t* new_layer = xui_element_layer_add ( x + title_pad_x, layer->line_y, layer->width, layer->height, layer->line_padding_x, layer->line_padding_y, &style );
    layer->line_y = border_height;
    std_unused_m ( new_layer );
}

void xui_element_section_end ( xui_workload_h workload ) {
    std_unused_m ( workload );
    xui_element_layer_t* layer = &xui_element_state->layers[xui_element_state->layer_count - 1];
    uint32_t title_pad_x = 10;
    layer->x -= title_pad_x;
    layer->width += title_pad_x;
    layer->line_height = 4;
    xui_element_newline();
    //xui_element_layer_pop();
}

void xui_element_newline ( void ) {
    xui_element_layer_t* layer = &xui_element_state->layers[xui_element_state->layer_count - 1];

    if ( layer->line_y + layer->line_height > layer->height ) {
        return;
    }

    layer->line_y += layer->line_height;
    layer->line_offset = 0;
    layer->line_height = 0;
    layer->line_offset_rev = 0;
}

static void xui_element_layer_align ( uint32_t* x, uint32_t* y, uint32_t width, uint32_t height, const xui_style_t* style ) {
    xui_element_layer_t* layer = &xui_element_state->layers[xui_element_state->layer_count - 1];

    *y = layer->line_y + layer->line_padding_y;

    if ( style->horizontal_alignment == xui_horizontal_alignment_left_to_right_m ) {
        *x = layer->line_offset + layer->line_padding_x;
    } else if ( style->horizontal_alignment == xui_horizontal_alignment_right_to_left_m ) {
        *x = layer->width - layer->line_offset_rev - layer->line_padding_x - width;
    } else {
        std_log_error_m ( "Unknown style alignment" );
    }
}

void xui_element_label ( xui_workload_h workload, xui_label_state_t* state ) {
    xui_style_t style = xui_element_inherit_style ( &state->style );

    xui_element_layer_t* layer = &xui_element_state->layers[xui_element_state->layer_count - 1];

    uint32_t x = layer->line_offset + layer->line_padding_x;
    uint32_t y = layer->line_y;// + layer->line_padding_y;
    uint32_t width = xui_element_string_width ( state->text, style.font, state->height );

    xui_element_layer_align ( &x, &y, width, state->height, &style );

    if ( xui_element_layer_add_element ( width, state->height, &style ) ) {
        xui_element_draw_string ( workload, style.font, state->text, layer->x + x, layer->y + y, state->height, state->sort_order );
    }
}

void xui_element_switch ( xui_workload_h workload, xui_switch_state_t* state ) {
    xui_style_t style = xui_element_inherit_style ( &state->style );
    
    xui_element_layer_t* layer = &xui_element_state->layers[xui_element_state->layer_count - 1];

    uint32_t padding = 2;
    uint32_t inner_padding = 1;
    uint32_t x, y;
    xui_element_layer_align ( &x, &y, state->width, state->height, &style );

    if ( !xui_element_layer_add_element ( state->width, state->height, &style ) ) {
        return;
    }

    if ( xui_element_layer_cursor_test ( x, y, state->width, state->height ) ) {
        xui_element_acquire_hovered ( state->id, 0 );

        if ( xui_element_cursor_click() ) {
            xui_element_acquire_active ( state->id, 0 );
        }
    } else {
        xui_element_release_hovered ( state->id );
    }

    // state update
    // TODO change value on mouse release (!xui_element_state->mouse_down seems to cause issues of non-registered clicks?)
    if ( xui_element_state->active_id == state->id && xui_element_state->mouse_down ) {
        if ( xui_element_state->hovered_id == state->id ) {
            state->value = ! ( state->value );
        }

        xui_element_release_active ( state->id );
    }

    // draw
    //uint32_t x_offset = state->value ? state->width : 0;
    //float color_scale = state->value ? 0.5 : 0.25f;

    xui_element_draw_rect ( workload, xui_color_rgb_mul_m ( style.color, 0.5f ), layer->x + x, layer->y + y, state->width, state->height, state->sort_order );
    xui_element_draw_rect ( workload, style.color, layer->x + x + padding, layer->y + y + padding, state->width - padding * 2, state->height - padding * 2, state->sort_order );

    if ( state->value ) {
        xui_element_draw_rect ( workload, xui_color_rgb_mul_m ( style.color, 0.5f ), layer->x + x + padding + inner_padding, layer->y + y + padding + inner_padding, state->width - padding * 2 - inner_padding * 2, state->height - padding * 2 - inner_padding * 2, state->sort_order );
        //xui_element_draw_tri ( workload, style->color, 
        //    layer->x + x + padding + inner_padding, layer->y + y + state->height / 2,
        //    layer->x + x + state->width / 2, layer->y + y + padding + inner_padding,
        //    layer->x + x + state->width - padding - inner_padding, layer->y + y + state->height / 2,
        //    state->sort_order
        //);
        //xui_element_draw_tri ( workload, style->color, 
        //    layer->x + x + padding + inner_padding, layer->y + y + state->height / 2,
        //    layer->x + x + state->width - padding - inner_padding, layer->y + y + state->height / 2,
        //    layer->x + x + state->width / 2, layer->y + y + state->height - padding - inner_padding,
        //    state->sort_order
        //);
    }

#if 0
    xui_element_draw_rect ( workload, xui_color_rgb_mul_m ( style->color, color_scale ), layer->x + x, layer->y + y, state->width * 2 + padding * 2, state->height + padding * 2, state->sort_order );
    xui_element_draw_rect ( workload, style->color, layer->x + x + x_offset + padding, layer->y + y + padding, state->width, state->height, state->sort_order );
#endif
}

void xui_element_slider ( xui_workload_h workload, xui_slider_state_t* state ) {
    xui_style_t style = xui_element_inherit_style ( &state->style );
    
    xui_element_layer_t* layer = &xui_element_state->layers[xui_element_state->layer_count - 1];

    uint32_t padding = 2;
    uint32_t x, y;
    xui_element_layer_align ( &x, &y, state->width, state->height, &style );

    if ( !xui_element_layer_add_element ( state->width, state->height, &style ) ) {
        return;
    }

    if ( xui_element_layer_cursor_test ( x, y, state->width, state->height ) ) {
        xui_element_acquire_hovered ( state->id, 0 );

        if ( xui_element_cursor_click() ) {
            xui_element_acquire_active ( state->id, 0 );
        }
    } else {
        xui_element_release_hovered ( state->id );
    }

    // state update
    uint32_t handle_width = state->width / 10;
    uint32_t range = state->width - handle_width - padding * 2;

    if ( xui_element_state->active_id == state->id ) {
        int32_t value = xui_element_state->input_state.cursor_x - x - layer->x - padding - handle_width / 2;

        if ( value < 0 ) {
            value = 0;
        }

        if ( value > range ) {
            value = range;
        }

        state->value = ( float ) value / range;

        if ( !xui_element_state->mouse_down ) {
            xui_element_release_active ( state->id );
        }
    }

    // Draw
    uint32_t x_offset = range * state->value + padding;
    xui_element_draw_rect ( workload, xui_color_rgb_mul_m ( style.color, 0.5f ), layer->x + x, layer->y + y, state->width, state->height, state->sort_order );
    xui_element_draw_rect ( workload, style.color, layer->x + x + x_offset, layer->y + y + padding, handle_width, state->height - padding * 2, state->sort_order );
}

void xui_element_button ( xui_workload_h workload, xui_button_state_t* state ) {
    xui_style_t style =  xui_element_inherit_style ( &state->style );
    
    xui_element_layer_t* layer = &xui_element_state->layers[xui_element_state->layer_count - 1];

    uint32_t x, y;
    xui_element_layer_align ( &x, &y, state->width, state->height, &style );

    if ( !xui_element_layer_add_element ( state->width, state->height, &style ) ) {
        return;
    }

    if ( xui_element_layer_cursor_test ( x, y, state->width, state->height ) ) {
        xui_element_acquire_hovered ( state->id, 0 );

        if ( xui_element_cursor_click() ) {
            xui_element_acquire_active ( state->id, 0 );
        }
    } else {
        xui_element_release_hovered ( state->id );
    }

    bool pressed = false;

    if ( !xui_element_state->mouse_down && xui_element_state->active_id == state->id ) {
        if ( xui_element_state->hovered_id == state->id ) {
            pressed = true;
        }

        xui_element_release_active ( state->id );
    }

    state->pressed = pressed;

    if ( xui_element_state->active_id == state->id ) {
        state->down = true;
    } else {
        state->down = false;
    }

    // draw
    float color_scale = xui_element_state->active_id == state->id ? 0.35f : 1.f;

    xui_element_draw_rect ( workload, xui_color_rgb_mul_m ( style.color, color_scale ), layer->x + x, layer->y + y, state->width, state->height, state->sort_order );

    uint32_t text_width = xui_element_string_width ( state->text, style.font, style.font_height );
    xui_element_draw_string ( workload, style.font, state->text, layer->x + x + ( state->width - text_width ) / 2, layer->y + y + ( state->height - style.font_height ) / 3 /*TODO precompute proper text height*/, style.font_height, state->sort_order );
}

void xui_element_select ( xui_workload_h workload, xui_select_state_t* state ) {
    xui_style_t style = xui_element_inherit_style ( &state->style );
    
    xui_element_layer_t* layer = &xui_element_state->layers[xui_element_state->layer_count - 1];

    uint32_t x, y;
    xui_element_layer_align ( &x, &y, state->width, state->height, &style );

    if ( !xui_element_layer_add_element ( state->width, state->height, &style ) ) {
        return;
    }

    if ( xui_element_state->active_id == state->id ) {
        if ( xui_element_layer_cursor_test ( x, y, state->width, state->height * ( state->item_count + 1 ) ) ) {
            xui_element_acquire_hovered ( state->id, 0 );

            if ( xui_element_cursor_click() ) {
                uint32_t y_offset = xui_element_state->input_state.cursor_y - y - layer->y;

                if ( y_offset > state->height ) {
                    state->item_idx = y_offset / state->height - 1;
                }

                xui_element_release_active ( state->id );
            }
        } else {
            xui_element_release_hovered ( state->id );

            if ( xui_element_state->mouse_down ) {
                xui_element_release_active ( state->id );
            }
        }
    } else {
        if ( xui_element_layer_cursor_test ( x, y, state->width, state->height ) ) {
            xui_element_acquire_hovered ( state->id, 0 );

            if ( xui_element_cursor_click() ) {
                xui_element_acquire_active ( state->id, 0 );
            }
        } else {
            xui_element_release_hovered ( state->id );
        }
    }

    xui_element_draw_rect ( workload, style.color, layer->x + x, layer->y + y, state->width, state->height, state->sort_order );
    uint32_t text_width = xui_element_string_width ( state->items[state->item_idx], style.font, style.font_height );
    xui_element_draw_string ( workload, style.font, state->items[state->item_idx], layer->x + x + ( state->width - text_width ) / 2, layer->y + y + ( state->height - style.font_height ) / 3, style.font_height, state->sort_order );

    if ( xui_element_state->active_id == state->id ) {
        xui_element_draw_rect ( workload, xui_color_rgb_mul_m ( style.color, 0.5f ), layer->x + x, layer->y + y + state->height, state->width, state->height * state->item_count, state->sort_order );

        // TODO use this to highlight hover
        //xui_element_draw_rect ( workload, xui_color_rgb_mul_m ( style->color, 0.75f ), layer->x + x, layer->y + y + state->height * ( state->item_idx + 1 ), state->width, state->height, state->sort_order );

        for ( uint32_t i = 0; i < state->item_count; ++i ) {
            uint32_t text_width = xui_element_string_width ( state->items[state->item_idx], style.font, style.font_height );
            xui_element_draw_string ( workload, style.font, state->items[i], layer->x + x + ( state->width - text_width ) / 2, layer->y + y + ( state->height - style.font_height ) / 3 + state->height * ( i + 1 ), style.font_height, state->sort_order );
        }
    }
}

uint64_t xui_element_get_active_id ( void ) {
    return xui_element_state->active_layer;
}

#if 0
void xui_element_text ( xui_workload_h workload, xui_text_state_t* state, const xui_style_t* style ) {
    xui_element_layer_t* layer = &xui_element_state->layers[xui_element_state->layer_count - 1];

    uint32_t y = layer->line_y + layer->line_padding_y;
    uint32_t x;

    if ( style->horizontal_alignment == xui_horizontal_alignment_left_to_right_m ) {
        x = layer->line_offset + layer->line_padding_x;
    } else if ( style->horizontal_alignment == xui_horizontal_alignment_right_to_left_m ) {
        x = layer->width - layer->line_offset_rev - layer->line_padding_x - state->width;
    }

    if ( !xui_element_layer_add_element ( state->width, state->height, style ) ) {
        return;
    }

    if ( xui_element_layer_cursor_test ( x, y, state->width, state->height ) ) {
        xui_element_state->hovered_id = state->id;

        if ( xui_element_state->mouse_down && xui_element_state->mouse_down_tick == xui_element_state->current_tick ) {
            xui_element_state->active_id = state->id;
        }
    }

    // state update
    if ( xui_element_state->active_id = state->id ) {

    }

    std_unused_m ( state );
    std_unused_m ( workload );
    std_unused_m ( style );
}
#endif
