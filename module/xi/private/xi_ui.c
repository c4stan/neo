#include "xi_ui.h"

#include "xi_workload.h"
#include "xi_font.h"

#include <std_log.h>

std_warnings_ignore_m ( "-Wunused-function" )

static xi_ui_module_state_t* xi_ui_state;

void xi_ui_load ( xi_ui_module_state_t* state ) {
    xi_ui_state = state;

    std_mem_zero_m ( xi_ui_state );
}

void xi_ui_reload ( xi_ui_module_state_t* state ) {
    xi_ui_state = state;
}

void xi_ui_unload ( void ) {
}

static bool xi_ui_cursor_test ( int64_t x, int64_t y, int64_t width, int64_t height ) {
    int64_t cursor_x = xi_ui_state->input_state.cursor_x;
    int64_t cursor_y = xi_ui_state->input_state.cursor_y;

    if ( cursor_x < x || cursor_x > x + width || cursor_y < y || cursor_y > y + height ) {
        return false;
    }

    return true;
}

static bool xi_ui_layer_cursor_test ( int64_t x, int64_t y, int64_t width, int64_t height ) {
    xi_ui_layer_t* layer = &xi_ui_state->layers[xi_ui_state->layer_count - 1];
    x += layer->x;
    y += layer->y;

    return xi_ui_cursor_test ( x, y, width, height );
}

static bool xi_ui_cursor_click() {
    return xi_ui_state->mouse_down && xi_ui_state->mouse_down_tick == xi_ui_state->current_tick;
}

// x,y coords are expected to be relative to the parent layer
// TODO rename to _push
static xi_ui_layer_t* xi_ui_layer_add ( uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t padding_x, uint32_t padding_y, const xi_style_t* style ) {
    // if there's a previous layer take that into account
    if ( xi_ui_state->layer_count > 0 ) {
        xi_ui_layer_t* layer = &xi_ui_state->layers[xi_ui_state->layer_count - 1];

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

    xi_ui_layer_t* layer = &xi_ui_state->layers[xi_ui_state->layer_count++];
    std_assert_m ( xi_ui_state->layer_count <= xi_ui_max_layers_m );

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

    if ( xi_ui_cursor_test ( x, y, width, height ) && xi_ui_cursor_click() ) {
        xi_ui_state->active_layer = xi_ui_state->layer_count;
    }

    return layer;
}

static void xi_ui_layer_pop ( void ) {
    xi_ui_state->layer_count--;
}

static void xi_ui_layer_pop_all ( void ) {
    xi_ui_state->layer_count = 0;
}

void xi_ui_newline ( void ) {
    xi_ui_layer_t* layer = &xi_ui_state->layers[xi_ui_state->layer_count - 1];

    if ( layer->line_y + layer->line_height > layer->height ) {
        return;
    }

    layer->line_y += layer->line_height;
    layer->line_offset = 0;
    layer->line_height = 0;
    layer->line_offset_rev = 0;
}

static bool xi_ui_layer_add_section ( uint32_t width, uint32_t height ) {
    xi_ui_layer_t* layer = &xi_ui_state->layers[xi_ui_state->layer_count - 1];

    if ( xi_ui_state->in_section && xi_ui_state->minimized_section ) {
        return false;
    }

    // check for enough horizontal space
    if ( width + layer->line_offset + layer->line_offset_rev > layer->width ) {
        return false;
    }

    // check for enough vertical space
    if ( height + layer->line_y > layer->height ) {
        return false;
    }

    // update line height
    if ( height > layer->line_height ) {
        layer->line_height = height;
    }

    xi_ui_newline();

    return true;
}

static void xi_ui_layer_align ( uint32_t* x, uint32_t* y, uint32_t width, uint32_t height, const xi_style_t* style ) {
    xi_ui_layer_t* layer = &xi_ui_state->layers[xi_ui_state->layer_count - 1];

    if ( style->horizontal_alignment == xi_horizontal_alignment_left_to_right_m ) {
        *x = layer->line_offset + layer->line_padding_x;
    } else if ( style->horizontal_alignment == xi_horizontal_alignment_right_to_left_m ) {
        *x = layer->width - layer->line_offset_rev - layer->line_padding_x - width;
    }

    if ( style->vertical_alignment != xi_vertical_alignment_unaligned_m ) {
        *y = layer->line_y + layer->line_padding_y;
    }

    if ( style->vertical_alignment == xi_vertical_alignment_centered_m ) {
        if ( layer->line_height > height ) {
            *y += ( layer->line_height - height ) / 2;
        }
    } else if ( style->vertical_alignment == xi_vertical_alignment_bottom_m ) {
        if ( layer->line_height > height ) {
            *y += ( layer->line_height - height );
        }
    }

    *x = layer->x + *x;
    *y = layer->y + *y;
}

// try to make space for a new element in a layer.
// elements get appended horizontally on the current line, from left to right
static bool xi_ui_layer_add_element ( uint32_t* x, uint32_t* y, uint32_t width, uint32_t height, const xi_style_t* style ) {
    xi_ui_layer_t* layer = &xi_ui_state->layers[xi_ui_state->layer_count - 1];

    if ( xi_ui_state->in_section && xi_ui_state->minimized_section ) {
        return false;
    }

    // apply padding to element size
    // TODO keep or remove?
    //width += layer->line_padding_x * 2;
    //height += layer->line_padding_y * 2;

    // check for enough horizontal space
    if ( width + layer->line_offset + layer->line_offset_rev > layer->width ) {
        return false;
    }

    // check for enough vertical space
    if ( height + layer->line_y > layer->height ) {
        return false;
    }

    xi_ui_layer_align ( x, y, width, height, style );

    // update line height
    if ( height > layer->line_height ) {
        layer->line_height = height;
    }

    // update horizontal layer offset
    if ( style->horizontal_alignment == xi_horizontal_alignment_left_to_right_m ) {
        layer->line_offset += width;
    } else if ( style->horizontal_alignment == xi_horizontal_alignment_right_to_left_m ) {
        layer->line_offset_rev += width;
    }

    return true;
}

static bool xi_ui_acquire_active ( uint64_t id, uint32_t sub_id ) {
    if ( xi_ui_state->active_id == 0 && !xi_ui_state->cleared_active ) {
        xi_ui_state->active_id = id;
        xi_ui_state->active_sub_id = sub_id;
        return true;
    }

    return false;
}

static bool xi_ui_acquire_hovered ( uint64_t id, uint32_t sub_id ) {
    // same as per active
    if ( xi_ui_state->hovered_id == 0 && !xi_ui_state->cleared_hovered ) {
        xi_ui_state->hovered_id = id;
        xi_ui_state->hovered_sub_id = sub_id;
        return true;
    }

    return false;
}

static bool xi_ui_release_active ( uint64_t id ) {
    if ( xi_ui_state->active_id == id ) {
        xi_ui_state->active_id = 0;
        xi_ui_state->active_sub_id = 0;
        xi_ui_state->cleared_active = true;

        return true;
    }

    return false;
}

static bool xi_ui_release_hovered ( uint64_t id ) {
    if ( xi_ui_state->hovered_id == id ) {
        xi_ui_state->hovered_id = 0;
        xi_ui_state->hovered_sub_id = 0;
        xi_ui_state->cleared_hovered = true;

        return true;
    }

    return false;
}

static void xi_ui_draw_rect ( xi_workload_h workload, xi_color_t color, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint64_t sort_order ) {
    xi_draw_rect_t rect = xi_default_draw_rect_m;
    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;
    rect.color = color;
    rect.sort_order = sort_order;
    xi_workload_cmd_draw ( workload, &rect, 1 );
}

static void xi_ui_draw_tri ( xi_workload_h workload, xi_color_t color, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint64_t sort_order ) {
    xi_draw_tri_t tri = xi_default_draw_tri_m;
    tri.xy0[0] = x0;
    tri.xy0[1] = y0;
    tri.xy1[0] = x1;
    tri.xy1[1] = y1;
    tri.xy2[0] = x2;
    tri.xy2[1] = y2;
    tri.color = color;
    tri.sort_order = sort_order;
    xi_workload_cmd_draw_tri ( workload, &tri, 1 );
}

static uint32_t xi_ui_draw_string ( xi_workload_h workload, xi_font_h font, const char* text, uint32_t x, uint32_t y, uint64_t sort_order ) {
    size_t len = std_str_len ( text );
    // TODO assert on len

    xi_font_info_t font_info;
    xi_font_get_info ( &font_info, font );
    float scale = 0.5;//( float ) height / font_info.pixel_height;

    uint32_t x_offset = 0;
    float fx = x;
    float fy = y + font_info.pixel_height + font_info.descent;

    for ( uint32_t i = 0; i < len; ++i ) {
        xi_font_char_box_t box = xi_font_char_box_get ( &fx, &fy, font, text[i] );

        xi_draw_rect_t rect = xi_default_draw_rect_m;
        //rect.x = x + x_offset;
        //rect.y = y + ( font_info.pixel_height - box.height ) * scale;
        //rect.width = box.width * scale;
        //rect.height = box.height * scale;
        rect.x = box.xy0[0];
        rect.y = box.xy0[1];
        rect.width = box.xy1[0] - box.xy0[0];
        rect.height = box.xy1[1] - box.xy0[1];
        rect.color = xi_color_white_m;
        rect.texture = xi_font_atlas_get ( font );
        rect.linear_sampler_filter = false;
        rect.uv0[0] = box.uv0[0];
        rect.uv0[1] = box.uv0[1];
        rect.uv1[0] = box.uv1[0];
        rect.uv1[1] = box.uv1[1];
        rect.sort_order = sort_order;

        x_offset += box.width * scale;

        xi_workload_cmd_draw ( workload, &rect, 1 ); // TODO batch? change the api to only take 1?
    }

    uint32_t width = fx - x;

    //xi_ui_draw_rect ( workload, xi_color_red_m, x, y, width, font_info.pixel_height, 0 );

    return width;
}

// TODO return height too
static uint32_t xi_ui_string_width ( const char* text, xi_font_h font ) {
    size_t len = std_str_len ( text );
    // TODO assert on len

    xi_font_info_t font_info;
    xi_font_get_info ( &font_info, font );
    float scale = 1;//( float ) pixel_height / font_info.pixel_height;

    uint32_t width = 0;
    float fx = 0;
    float fy = 0;

    for ( uint32_t i = 0; i < len; ++i ) {
        xi_font_char_box_t box = xi_font_char_box_get ( &fx, &fy, font, text[i] );
        width += box.width * scale;
    }

    return ( uint32_t ) fx;
}

void xi_ui_update ( const wm_window_info_t* window_info, const wm_input_state_t* input_state ) {
    //xi_ui_state->base_x = -window_info->x - window_info->border_left;
    //xi_ui_state->base_y = -window_info->y - window_info->border_top;
    //xi_ui_state->base_width = window_info->width;
    //xi_ui_state->base_height = window_info->height;
    xi_ui_state->os_window_width = window_info->width;
    xi_ui_state->os_window_height = window_info->height;

    xi_ui_state->current_tick = std_tick_now();

    xi_ui_state->mouse_delta_x = input_state->cursor_x - xi_ui_state->input_state.cursor_x;
    xi_ui_state->mouse_delta_y = input_state->cursor_y - xi_ui_state->input_state.cursor_y;

    if ( input_state->mouse[wm_mouse_state_left_m] && !xi_ui_state->mouse_down ) {
        xi_ui_state->mouse_down_tick = xi_ui_state->current_tick;
    }

    xi_ui_state->mouse_down = input_state->mouse[wm_mouse_state_left_m];

    xi_ui_state->input_state = *input_state;

    xi_ui_state->hovered_id = 0;

    if ( !xi_ui_state->mouse_down ) {
        xi_ui_state->active_layer = 0;
    }

    xi_ui_state->cleared_hovered = false;
    xi_ui_state->cleared_active = false;

    xi_ui_state->in_section = false;
    xi_ui_state->minimized_section = false;
}

void xi_ui_update_end ( void ) {
    // TODO remove?
}

xi_style_t xi_ui_inherit_style ( const xi_style_t* style ) {
    if (xi_ui_state->layer_count == 0) {
        return *style;
    }

    xi_style_t* parent = &xi_ui_state->layers[xi_ui_state->layer_count - 1].style;
    xi_style_t result;

    result.font = style->font != xi_null_handle_m ? style->font : parent->font;
    result.font_height = style->font_height != 0 ? style->font_height : parent->font_height;
    result.color = style->color.u32 != 0 ? style->color : parent->color;
    result.font_color = style->font_color.u32 != 0 ? style->font_color : parent->font_color;
    result.horizontal_alignment = style->horizontal_alignment != xi_horizontal_alignment_invalid_m ? style->horizontal_alignment : parent->horizontal_alignment;
    result.vertical_alignment = style->vertical_alignment != xi_vertical_alignment_invalid_m ? style->vertical_alignment : parent->vertical_alignment;

    return result;
}

void xi_ui_window_begin ( xi_workload_h workload, xi_window_state_t* state ) {
    xi_style_t style = xi_ui_inherit_style ( &state->style );

    xi_font_info_t font_info;
    xi_font_get_info ( &font_info, style.font );

    uint32_t title_pad_x = 10;
    uint32_t title_pad_y = 5;
    uint32_t border_height = font_info.pixel_height + title_pad_y * 2;

    xi_ui_layer_t* layer = xi_ui_layer_add ( state->x, state->y, state->width, state->height, state->padding_x, state->padding_y, &style );
    layer->line_y = border_height;

    // drag
    if ( xi_ui_layer_cursor_test ( 0, 0, layer->width, border_height ) ) {
        xi_ui_acquire_hovered ( state->id, 0 );

        if ( xi_ui_cursor_click() ) {
            xi_ui_acquire_active ( state->id, 0 );
        }
    } else {
        xi_ui_release_hovered ( state->id );
    }

    if ( xi_ui_state->active_id == state->id && xi_ui_state->active_sub_id == 0 ) {
        //float delta = std_tick_to_milli_f32 ( xi_ui_state.current_tick - xi_ui_state.mouse_down_tick );

        //if ( delta > 100.f ) {
        state->x += xi_ui_state->mouse_delta_x;
        state->y += xi_ui_state->mouse_delta_y;
        //}

        if ( state->x < 0 ) {
            state->x = 0;
        }

        if ( state->x + state->width > xi_ui_state->os_window_width ) {
            state->x = xi_ui_state->os_window_width - state->width;
        }

        if ( state->y < 0 ) {
            state->y = 0;
        }

        if ( state->y + state->height > xi_ui_state->os_window_height ) {
            state->y = xi_ui_state->os_window_height - state->height;
        }
    }

    // resize
    if ( xi_ui_layer_cursor_test ( layer->width - 20, layer->height - 20, 20, 20 ) ) {
        if ( xi_ui_state->mouse_down && xi_ui_state->mouse_down_tick == xi_ui_state->current_tick ) {
            xi_ui_acquire_active ( state->id, 1 );
        }
    }

    uint32_t corner_size = 15;

    if ( xi_ui_state->active_id == state->id && xi_ui_state->active_sub_id == 1 ) {
        //float delta = std_tick_to_milli_f32 ( xi_ui_state.current_tick - xi_ui_state.mouse_down_tick );

        //if ( delta > 100.f ) {
        //if ( xi_ui_state->input_state.cursor_x + xi_ui_state->base_x > layer->x + title_pad_x || state->width > title_pad_x + 10 ) {
        int32_t width = state->width;
        int32_t height = state->height;

        int32_t min_width = title_pad_x + 10;
        int32_t min_height = border_height + corner_size;

        if ( width > min_width ) {
            width += xi_ui_state->mouse_delta_x;
        } else {
            if ( xi_ui_state->input_state.cursor_x > state->x + min_width ) {
                uint32_t delta = xi_ui_state->input_state.cursor_x - ( state->x + min_width );
                width += delta;
            }
        }

        if ( height > min_height ) {
            height += xi_ui_state->mouse_delta_y;
        } else {
            if ( xi_ui_state->input_state.cursor_y > state->y + min_height ) {
                uint32_t delta = xi_ui_state->input_state.cursor_y - ( state->y + min_height );
                height += delta;
            }
        }

        //if ( width > min_width || xi_ui_state->input_state.cursor_x > state->x + min_width ) {
        //    width += xi_ui_state->mouse_delta_x;
        //}

        //if ( height > min_height || xi_ui_state->input_state.cursor_y > state->y + min_height ) {
        //    height += xi_ui_state->mouse_delta_y;
        //}

        //}

        //if ( xi_ui_state->input_state.cursor_y + xi_ui_state->base_y > layer->y + border_height || state->height > border_height ) {
        //}

        if ( state->x + width > xi_ui_state->os_window_width ) {
            width = xi_ui_state->os_window_width - state->x;
        }

        if ( state->y + height > xi_ui_state->os_window_height ) {
            height = xi_ui_state->os_window_height - state->y;
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

    if ( xi_ui_state->active_id == state->id && !xi_ui_state->mouse_down ) {
        xi_ui_release_active ( state->id );
    }

    // draw
    //  base background
    xi_ui_draw_rect ( workload, style.color, layer->x, layer->y, layer->width, layer->height, state->sort_order );

    //  title bar
    xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style.color, 0.3 ), layer->x, layer->y, layer->width, font_info.pixel_height + title_pad_y * 2, state->sort_order );

    //  title text
    xi_ui_draw_string ( workload, style.font, state->title, layer->x + title_pad_x, layer->y + title_pad_y / 2, state->sort_order );

#if 0
    // title tabs
    for ( uint32_t i = 0; i < state->tab_count; ++i ) {
        uint32_t width = xi_ui_string_width ( state->tabs[i], style->font, style->font_height );

        if ( state->tab_count > 1 ) {
            // tab hover
            if ( xi_ui_layer_cursor_test ( x_offset, 0, width + title_pad_x * 2, style->font_height + title_pad_y * 2 ) ) {
                xi_ui_state->hovered_id = state->id;
                xi_ui_state->hovered_sub_id = 2 + i;

                xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style->color, 0 ), layer->x + x_offset, layer->y, width + title_pad_x * 2, style->font_height + title_pad_y * 2, state->sort_order );

                if ( xi_ui_state->mouse_down && xi_ui_state->mouse_down_tick == xi_ui_state->current_tick ) {
                    xi_ui_state->active_id = state->id;
                    xi_ui_state->active_sub_id = 2 + i;
                }
            }
        }

        // tab name
        // TODO use labels and xi_ui_layer_add_element?
        x_offset += title_pad_x;
        x_offset += xi_ui_draw_string ( workload, style->font, state->tabs[i], layer->x + x_offset, layer->y + title_pad_y, style->font_height, state->sort_order );
        x_offset += title_pad_x;
    }

    // tab idx update
    if ( !xi_ui_state->mouse_down ) {
        if ( xi_ui_state->active_id == state->id && xi_ui_state->active_sub_id >= 2 ) {
            if ( xi_ui_state->hovered_id == state->id && xi_ui_state->hovered_sub_id == xi_ui_state->active_sub_id ) {
                state->tab_idx = xi_ui_state->active_sub_id - 2;
            }
        }
    }

#endif

    //  resize corner
    xi_ui_draw_tri ( workload, xi_color_rgb_mul_m ( style.color, 0.5 ),
        layer->x + layer->width, layer->y + layer->height - corner_size,
        layer->x + layer->width, layer->y + layer->height,
        layer->x + layer->width - corner_size, layer->y + layer->height,
        state->sort_order  );
}

void xi_ui_window_end ( xi_workload_h workload ) {
    std_unused_m ( workload );
    xi_ui_layer_pop_all();
}

void xi_ui_section_begin ( xi_workload_h workload, xi_section_state_t* state ) {
    xi_style_t style = xi_ui_inherit_style ( &state->style );

    xi_font_info_t font_info;
    xi_font_get_info ( &font_info, style.font );

    uint32_t title_pad_x = 10;
    uint32_t title_pad_y = 0;

    //state->height = 20;

    xi_ui_layer_t* layer = &xi_ui_state->layers[xi_ui_state->layer_count - 1];
    uint32_t x = layer->line_offset;
    uint32_t y = layer->line_y;

    uint32_t header_height = std_max_u32 ( state->height, font_info.pixel_height ) + title_pad_y * 2;
    uint32_t triangle_width = header_height  * 0.8;
    uint32_t triangle_height = header_height * 0.8;
    uint32_t title_width = xi_ui_string_width ( state->title, style.font ) + triangle_width;

    if ( !xi_ui_layer_add_section ( title_width, header_height ) ) {
        return;
    }

    // state
    if ( xi_ui_layer_cursor_test ( x, y, layer->width, header_height ) ) {
        xi_ui_acquire_hovered ( state->id, 0 );

        if ( xi_ui_cursor_click() ) {
            xi_ui_acquire_active ( state->id, 0 );
        }
    } else {
        xi_ui_release_hovered ( state->id );
    }

    bool pressed = false;

    if ( !xi_ui_state->mouse_down && xi_ui_state->active_id == state->id ) {
        if ( xi_ui_state->hovered_id == state->id ) {
            pressed = true;
        }

        xi_ui_release_active ( state->id );
    }

    if ( pressed ) {
        state->minimized = !state->minimized;
    }

    std_assert_m ( !xi_ui_state->in_section );
    xi_ui_state->in_section = true;
    xi_ui_state->minimized_section = state->minimized;

    // draw
    //  header bar
    xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style.color, 0.6 ), layer->x + x, layer->y + y, layer->width, header_height, state->sort_order );

    if ( !state->minimized ) {
        xi_ui_draw_tri ( workload, xi_color_rgb_mul_m ( style.color, 1 ), 
            layer->x + x + title_pad_x,                         layer->y + y + ( header_height - triangle_height ) / 2, 
            layer->x + x + title_pad_x + triangle_width,        layer->y + y + ( header_height - triangle_height ) / 2, 
            layer->x + x + title_pad_x + triangle_width / 2,    layer->y + y + ( header_height - triangle_height ) / 2 + triangle_height, 
            state->sort_order );
    } else {
        xi_ui_draw_tri ( workload, xi_color_rgb_mul_m ( style.color, 1 ), 
            layer->x + x + title_pad_x,                         layer->y + y + ( header_height - triangle_height ) / 2,
            layer->x + x + title_pad_x + triangle_width,        layer->y + y + header_height / 2, 
            layer->x + x + title_pad_x,                         layer->y + y + ( header_height - triangle_height ) / 2 + triangle_height, 
            state->sort_order );
    }

    //  title text
    xi_ui_draw_string ( workload, style.font, state->title, layer->x + x + title_pad_x * 2 + triangle_width, layer->y + y + title_pad_y / 2 + ( header_height - font_info.pixel_height ) / 2, state->sort_order );

    // new layer
    //title_pad_x = 10;
    xi_ui_layer_add ( x + title_pad_x, layer->line_y, layer->width, layer->height, layer->line_padding_x, layer->line_padding_y, &style );
}

void xi_ui_section_end ( xi_workload_h workload ) {
    std_unused_m ( workload );

    if ( !xi_ui_state->in_section ) {
        return;
    }

    xi_ui_layer_t* layer = &xi_ui_state->layers[xi_ui_state->layer_count - 1];
    uint32_t title_pad_x = 10;
    layer->x -= title_pad_x;
    layer->width += title_pad_x;
    layer->line_height = 4;

    if ( !xi_ui_state->minimized_section ) {
        xi_ui_newline();
    }

    //std_assert_m ( xi_ui_state->in_section );
    xi_ui_state->in_section = false;
    xi_ui_state->minimized_section = false;
}

void xi_ui_label ( xi_workload_h workload, xi_label_state_t* state ) {
    xi_style_t style = xi_ui_inherit_style ( &state->style );

    xi_font_info_t font_info;
    xi_font_get_info ( &font_info, style.font );

    uint32_t x, y;
    uint32_t width = xi_ui_string_width ( state->text, style.font );
    uint32_t height = std_max_u32 ( state->height, font_info.pixel_height );

    if ( xi_ui_layer_add_element ( &x, &y, width, height, &style ) ) {
        xi_ui_draw_string ( workload, style.font, state->text, x, y, state->sort_order );
    }
}

void xi_ui_switch ( xi_workload_h workload, xi_switch_state_t* state ) {
    xi_style_t style = xi_ui_inherit_style ( &state->style );
    
    uint32_t padding = 2;
    uint32_t inner_padding = 1;
    uint32_t x, y;

    if ( !xi_ui_layer_add_element ( &x, &y, state->width, state->height, &style ) ) {
        return;
    }

    if ( xi_ui_cursor_test ( x, y, state->width, state->height ) ) {
        xi_ui_acquire_hovered ( state->id, 0 );

        if ( xi_ui_cursor_click() ) {
            xi_ui_acquire_active ( state->id, 0 );
        }
    } else {
        xi_ui_release_hovered ( state->id );
    }

    // state update
    // TODO change value on mouse release (!xi_ui_state->mouse_down seems to cause issues of non-registered clicks?)
    if ( xi_ui_state->active_id == state->id && xi_ui_state->mouse_down ) {
        if ( xi_ui_state->hovered_id == state->id ) {
            state->value = ! ( state->value );
        }

        xi_ui_release_active ( state->id );
    }

    // draw
    //uint32_t x_offset = state->value ? state->width : 0;
    //float color_scale = state->value ? 0.5 : 0.25f;

    xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style.color, 0.5f ), x, y, state->width, state->height, state->sort_order );
    xi_ui_draw_rect ( workload, style.color, x + padding, y + padding, state->width - padding * 2, state->height - padding * 2, state->sort_order );

    if ( state->value ) {
        xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style.color, 0.5f ), x + padding + inner_padding, y + padding + inner_padding, state->width - padding * 2 - inner_padding * 2, state->height - padding * 2 - inner_padding * 2, state->sort_order );
        //xi_ui_draw_tri ( workload, style->color, 
        //    layer->x + x + padding + inner_padding, layer->y + y + state->height / 2,
        //    layer->x + x + state->width / 2, layer->y + y + padding + inner_padding,
        //    layer->x + x + state->width - padding - inner_padding, layer->y + y + state->height / 2,
        //    state->sort_order
        //);
        //xi_ui_draw_tri ( workload, style->color, 
        //    layer->x + x + padding + inner_padding, layer->y + y + state->height / 2,
        //    layer->x + x + state->width - padding - inner_padding, layer->y + y + state->height / 2,
        //    layer->x + x + state->width / 2, layer->y + y + state->height - padding - inner_padding,
        //    state->sort_order
        //);
    }

#if 0
    xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style->color, color_scale ), layer->x + x, layer->y + y, state->width * 2 + padding * 2, state->height + padding * 2, state->sort_order );
    xi_ui_draw_rect ( workload, style->color, layer->x + x + x_offset + padding, layer->y + y + padding, state->width, state->height, state->sort_order );
#endif
}

void xi_ui_slider ( xi_workload_h workload, xi_slider_state_t* state ) {
    xi_style_t style = xi_ui_inherit_style ( &state->style );
    
    uint32_t padding = 2;
    uint32_t x, y;
    
    if ( !xi_ui_layer_add_element ( &x, &y, state->width, state->height, &style ) ) {
        return;
    }

    if ( xi_ui_cursor_test ( x, y, state->width, state->height ) ) {
        xi_ui_acquire_hovered ( state->id, 0 );

        if ( xi_ui_cursor_click() ) {
            xi_ui_acquire_active ( state->id, 0 );
        }
    } else {
        xi_ui_release_hovered ( state->id );
    }

    // state update
    uint32_t handle_width = state->width / 10;
    uint32_t range = state->width - handle_width - padding * 2;

    if ( xi_ui_state->active_id == state->id ) {
        int32_t value = xi_ui_state->input_state.cursor_x - x - padding - handle_width / 2;

        if ( value < 0 ) {
            value = 0;
        }

        if ( value > range ) {
            value = range;
        }

        state->value = ( float ) value / range;

        if ( !xi_ui_state->mouse_down ) {
            xi_ui_release_active ( state->id );
        }
    }

    // Draw
    uint32_t x_offset = range * state->value + padding;
    xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style.color, 0.5f ), x, y, state->width, state->height, state->sort_order );
    xi_ui_draw_rect ( workload, style.color, x + x_offset, y + padding, handle_width, state->height - padding * 2, state->sort_order );
}

void xi_ui_button ( xi_workload_h workload, xi_button_state_t* state ) {
    xi_style_t style =  xi_ui_inherit_style ( &state->style );

    xi_font_info_t font_info;
    xi_font_get_info ( &font_info, style.font );
    
    uint32_t text_width = xi_ui_string_width ( state->text, style.font );
    uint32_t width = std_max_u32 ( text_width, state->width );
    uint32_t height = std_max_u32 ( font_info.pixel_height, state->height );

    uint32_t x, y;

    if ( !xi_ui_layer_add_element ( &x, &y, width, height, &style ) ) {
        return;
    }

    // --

#if 1
    if ( xi_ui_cursor_test ( x, y, width, height ) ) {
        xi_ui_acquire_hovered ( state->id, 0 );

        if ( xi_ui_cursor_click() ) {
            xi_ui_acquire_active ( state->id, 0 );
        }
    } else {
        xi_ui_release_hovered ( state->id );
    }

    bool pressed = false;

    if ( !xi_ui_state->mouse_down && xi_ui_state->active_id == state->id ) {
        if ( xi_ui_state->hovered_id == state->id ) {
            pressed = true;
        }

        xi_ui_release_active ( state->id );
    }
#else
    bool pressed = xi_ui_update_pressed ( x, y, width, height, state->id, 0 );
#endif

    state->pressed = pressed;

    if ( xi_ui_state->active_id == state->id ) {
        state->down = true;
    } else {
        state->down = false;
    }

    // draw
    float color_scale = xi_ui_state->active_id == state->id ? 0.35f : 1.f;

    xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style.color, color_scale ), x, y, width, height, state->sort_order );

    xi_ui_draw_string ( workload, style.font, state->text, x + ( width - text_width ) / 2, y, state->sort_order );
}

void xi_ui_select ( xi_workload_h workload, xi_select_state_t* state ) {
    xi_style_t style = xi_ui_inherit_style ( &state->style );
   
    xi_font_info_t font_info;
    xi_font_get_info ( &font_info, style.font );

    uint32_t selected_text_width = xi_ui_string_width ( state->items[state->item_idx], style.font );
    uint32_t width = std_max_u32 ( selected_text_width, state->width );
    uint32_t height = std_max_u32 ( font_info.pixel_height, state->height );

    uint32_t x, y;

    if ( !xi_ui_layer_add_element ( &x, &y, width, height, &style ) ) {
        return;
    }

    if ( xi_ui_state->active_id == state->id ) {
        if ( xi_ui_cursor_test ( x, y, width, height * ( state->item_count + 1 ) ) ) {
            xi_ui_acquire_hovered ( state->id, 0 );

            if ( xi_ui_cursor_click() ) {
                uint32_t y_offset = xi_ui_state->input_state.cursor_y - y;

                if ( y_offset > height ) {
                    state->item_idx = y_offset / height - 1;
                }

                xi_ui_release_active ( state->id );
            }
        } else {
            xi_ui_release_hovered ( state->id );

            if ( xi_ui_state->mouse_down ) {
                xi_ui_release_active ( state->id );
            }
        }
    } else {
        if ( xi_ui_cursor_test ( x, y, width, height ) ) {
            xi_ui_acquire_hovered ( state->id, 0 );

            if ( xi_ui_cursor_click() ) {
                xi_ui_acquire_active ( state->id, 0 );
            }
        } else {
            xi_ui_release_hovered ( state->id );
        }
    }

    xi_ui_draw_rect ( workload, style.color, x, y, width, height, state->sort_order );
    uint32_t text_width = xi_ui_string_width ( state->items[state->item_idx], style.font );
    xi_ui_draw_string ( workload, style.font, state->items[state->item_idx], x + ( width - text_width ) / 2, y + ( height - font_info.pixel_height ) / 3, state->sort_order );

    if ( xi_ui_state->active_id == state->id ) {
        xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style.color, 0.5f ), x, y + height, width, height * state->item_count, state->sort_order );

        // TODO use this to highlight hover
        //xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style->color, 0.75f ), layer->x + x, layer->y + y + state->height * ( state->item_idx + 1 ), state->width, state->height, state->sort_order );

        for ( uint32_t i = 0; i < state->item_count; ++i ) {
            if ( xi_ui_cursor_test ( x, y + height * ( i + 1 ), width, height ) ) {
                xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style.color, 0.75f ), x, y + height * ( i + 1 ), width, height, state->sort_order );
                //xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style->color, 0.75f ), layer->x + x, layer->y + y + height * ( i + 1 ), width, height, state->sort_order );
            }
            uint32_t text_width = xi_ui_string_width ( state->items[state->item_idx], style.font );
            xi_ui_draw_string ( workload, style.font, state->items[i], x + ( width - text_width ) / 2, y + height * ( i + 1 ), state->sort_order );
        }
    }
}

uint64_t xi_ui_get_active_id ( void ) {
    return xi_ui_state->active_layer;
}

#if 0
void xi_ui_text ( xi_workload_h workload, xi_text_state_t* state, const xi_style_t* style ) {
    xi_ui_layer_t* layer = &xi_ui_state->layers[xi_ui_state->layer_count - 1];

    uint32_t y = layer->line_y + layer->line_padding_y;
    uint32_t x;

    if ( style->horizontal_alignment == xi_horizontal_alignment_left_to_right_m ) {
        x = layer->line_offset + layer->line_padding_x;
    } else if ( style->horizontal_alignment == xi_horizontal_alignment_right_to_left_m ) {
        x = layer->width - layer->line_offset_rev - layer->line_padding_x - state->width;
    }

    if ( !xi_ui_layer_add_element ( state->width, state->height, style ) ) {
        return;
    }

    if ( xi_ui_layer_cursor_test ( x, y, state->width, state->height ) ) {
        xi_ui_state->hovered_id = state->id;

        if ( xi_ui_state->mouse_down && xi_ui_state->mouse_down_tick == xi_ui_state->current_tick ) {
            xi_ui_state->active_id = state->id;
        }
    }

    // state update
    if ( xi_ui_state->active_id = state->id ) {

    }

    std_unused_m ( state );
    std_unused_m ( workload );
    std_unused_m ( style );
}
#endif
