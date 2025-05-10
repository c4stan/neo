#include "xi_ui.h"

#include "xi_workload.h"
#include "xi_font.h"

#include <sm_vector.h>
#include <sm_matrix.h>
#include <sm_quat.h>

// TODO
#include <math.h>

#include <std_log.h>
#include <std_platform.h>

std_warnings_ignore_m ( "-Wunused-function" )

static xi_ui_state_t* xi_ui_state;

void xi_ui_load ( xi_ui_state_t* state ) {
    xi_ui_state = state;

    std_mem_zero_m ( xi_ui_state );
    xi_ui_state->device = xg_null_handle_m;

    std_mem_zero_static_array_m ( xi_ui_state->windows_map_values );
    xi_ui_state->windows_map = std_static_hash_map_m ( xi_ui_state->windows_map_ids, xi_ui_state->windows_map_values );
}

void xi_ui_reload ( xi_ui_state_t* state ) {
    xi_ui_state = state;
}

void xi_ui_unload ( void ) {
    if ( xi_ui_state->device != xg_null_handle_m ) {
        xg_i* xg = std_module_get_m ( xg_module_name_m );
        xg_workload_h workload = xg->create_workload ( xi_ui_state->device );
        xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );
        xg_geo_util_free_data ( &xi_ui_state->transform_geo.cpu );
        xg_geo_util_free_gpu_data ( &xi_ui_state->transform_geo.gpu, resource_cmd_buffer );
        xg->submit_workload ( workload );
    }
}

static bool xi_ui_cursor_test ( int64_t x, int64_t y, int64_t width, int64_t height ) {
    int64_t cursor_x = xi_ui_state->update.input_state.cursor_x;
    int64_t cursor_y = xi_ui_state->update.input_state.cursor_y;

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
    return xi_ui_state->update.mouse_down && xi_ui_state->update.mouse_down_tick == xi_ui_state->update.current_tick;
}

// x,y coords are expected to be relative to the parent layer
// TODO rename to _push
static xi_ui_layer_t* xi_ui_layer_add ( int64_t x, int64_t y, uint32_t width, uint32_t height, uint32_t padding_x, uint32_t padding_y, const xi_style_t* style, uint64_t id ) {
    int64_t delta_x = x;
    int64_t delta_y = y;

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
    layer->delta_x = delta_x;
    layer->delta_y = delta_y;
    layer->width = width;
    layer->height = height;
    layer->line_height = 0;
    layer->line_offset = 0;
    layer->line_offset_rev = 0;
    layer->line_y = 0;
    layer->line_padding_x = padding_x;
    layer->line_padding_y = padding_y;
    layer->style = *style;
    layer->total_content_height = 0;

    if ( xi_ui_cursor_test ( x, y, width, height ) ) {
        xi_ui_state->hovered_layer = id;

        if ( xi_ui_cursor_click() ) {
            xi_ui_state->active_layer = id;
        }
    }

    return layer;
}

static void xi_ui_layer_pop ( void ) {
    xi_ui_state->layer_count--;
}

static void xi_ui_layer_pop_all ( void ) {
    xi_ui_state->layer_count = 0;
}

static void xi_ui_layer_add_content ( uint32_t height ) {
    for ( uint32_t i = 0; i < xi_ui_state->layer_count; ++i ) {
        xi_ui_layer_t* layer = &xi_ui_state->layers[i];
        layer->total_content_height += height;
    }
}

void xi_ui_newline ( void ) {
    xi_ui_layer_t* layer = &xi_ui_state->layers[xi_ui_state->layer_count - 1];

    if ( layer->line_y + layer->line_height > layer->height ) {
        //return;
    }

    xi_ui_layer_add_content ( layer->line_height );
    layer->line_y += layer->line_height;
    layer->line_offset = 0;
    layer->line_height = 0;
    layer->line_offset_rev = 0;
}

static bool xi_ui_layer_add_section ( uint32_t width, uint32_t height ) {
    xi_ui_layer_t* layer = &xi_ui_state->layers[xi_ui_state->layer_count - 1];

    bool result = true;

    if ( xi_ui_state->minimized_window ) {
        result = false;
        return result;
    }

    if ( xi_ui_state->in_section && xi_ui_state->minimized_section ) {
        result = false;
        return result;
    }

    // check for enough horizontal space
    if ( /*width +*/ layer->line_offset + layer->line_offset_rev > layer->width ) {
        result = false;
    }

    // check for enough vertical space
    if ( /*height +*/ layer->line_y > layer->height ) {
        result = false;
    }

    // update line height
    if ( height > layer->line_height ) {
        layer->line_height = height;
    }

    xi_ui_newline();

    return result;
}

static void xi_ui_layer_align ( int32_t* x, int32_t* y, uint32_t width, uint32_t height, const xi_style_t* style ) {
    xi_ui_layer_t* layer = &xi_ui_state->layers[xi_ui_state->layer_count - 1];

    int32_t rx = 0, ry = 0;

    uint32_t horizontal_margin = 0;
    if ( style->horizontal_margin != xi_style_margin_invalid_m ) {
        horizontal_margin = style->horizontal_margin;
    }

    if ( style->horizontal_alignment == xi_horizontal_alignment_left_to_right_m ) {
        rx = layer->line_offset + layer->line_padding_x + horizontal_margin;
    } else if ( style->horizontal_alignment == xi_horizontal_alignment_right_to_left_m ) {
        rx = layer->width - layer->line_offset_rev - layer->line_padding_x - width - horizontal_margin;
    }

    if ( style->vertical_alignment != xi_vertical_alignment_unaligned_m ) {
        ry = layer->line_y + layer->line_padding_y;
    }

    if ( style->vertical_alignment == xi_vertical_alignment_centered_m ) {
        if ( layer->line_height > height ) {
            ry += ( layer->line_height - height ) / 2;
        }
    } else if ( style->vertical_alignment == xi_vertical_alignment_bottom_m ) {
        if ( layer->line_height > height ) {
            ry += ( layer->line_height - height );
        }
    }

    *x = layer->x + rx;
    *y = layer->y + ry;
}

bool xi_ui_layer_row_hover_test ( uint32_t height ) {
    xi_ui_layer_t* layer = &xi_ui_state->layers[xi_ui_state->layer_count - 1];
    return xi_ui_layer_cursor_test ( 0, layer->line_y, layer->width, height );
}

// try to make space for a new element in a layer.
// elements get appended horizontally on the current line, from left to right
static bool xi_ui_layer_add_element ( int32_t* x, int32_t* y, uint32_t width, uint32_t height, const xi_style_t* style ) {
    xi_ui_layer_t* layer = &xi_ui_state->layers[xi_ui_state->layer_count - 1];

    bool result = true;

    if ( xi_ui_state->minimized_window ) {
        result = false;
    }

    if ( xi_ui_state->culled_section ) {
        result = false;
    }

    if ( xi_ui_state->in_section && xi_ui_state->minimized_section ) {
        result = false;
        return result;
    }

    // apply padding to element size
    // TODO keep or remove?
    //width += layer->line_padding_x * 2;
    //height += layer->line_padding_y * 2;

    if ( style->horizontal_border_margin != xi_style_border_margin_invalid_m ) {
        if ( style->horizontal_alignment == xi_horizontal_alignment_left_to_right_m ) {
            layer->line_offset = std_max_u32 ( layer->line_offset, style->horizontal_border_margin );
        } else if ( style->horizontal_alignment == xi_horizontal_alignment_right_to_left_m ) {
            layer->line_offset_rev = std_max_u32 ( layer->line_offset_rev, style->horizontal_border_margin );
        }
    }

    // check for enough horizontal space
    if ( /*width +*/ layer->line_offset + layer->line_offset_rev > layer->width ) {
        result = false;
    }

    // check for enough vertical space
    if ( /*height +*/ layer->line_y > layer->height ) {
        result = false;
    }

    xi_ui_layer_align ( x, y, width, height, style );

    // update line height
    if ( height > layer->line_height ) {
        layer->line_height = height;
    }

    uint32_t horizontal_margin = 0;
    if ( style->horizontal_margin != xi_style_margin_invalid_m ) {
        horizontal_margin = style->horizontal_margin;
    }

    // update horizontal layer offset
    if ( style->horizontal_alignment == xi_horizontal_alignment_left_to_right_m ) {
        layer->line_offset += width + horizontal_margin;
    } else if ( style->horizontal_alignment == xi_horizontal_alignment_right_to_left_m ) {
        layer->line_offset_rev += width + horizontal_margin;
    }

    if ( layer->line_y + height < 0 ) {
        result = false;
    }

    return result;
}

static bool xi_ui_acquire_keyboard ( uint64_t id ) {
    if ( xi_ui_state->keyboard_id != id ) {
        xi_ui_state->keyboard_id = id;
        return true;
    }

    return false;
}

static uint32_t xi_ui_focus_stack_push ( uint64_t id, uint32_t sub_id ) {
    uint32_t idx = xi_ui_state->focus_stack_count++;
    xi_ui_state->focus_stack_ids[idx] = id;
    xi_ui_state->focus_stack_sub_ids[idx] = sub_id;

    // Insert sort into the "stack" to keep same id entries sorted by sub_id.
    // This allows property fields to tab in the right order even if they get
    // added to the window in the opposite order.
    while ( idx > 0 ) {
        if ( xi_ui_state->focus_stack_ids[idx - 1] != id || xi_ui_state->focus_stack_sub_ids[idx - 1] <= sub_id ) {
            break;
        }
        idx -= 1;
        xi_ui_state->focus_stack_ids[idx + 1] = xi_ui_state->focus_stack_ids[idx];
        xi_ui_state->focus_stack_sub_ids[idx + 1] = xi_ui_state->focus_stack_sub_ids[idx];
        xi_ui_state->focus_stack_ids[idx] = id;
        xi_ui_state->focus_stack_sub_ids[idx] = sub_id;
    }
    return idx;
}

static bool xi_ui_release_keyboard ( uint64_t id ) {
    if ( xi_ui_state->keyboard_id == id ) {
        xi_ui_state->keyboard_id = 0;
        return true;
    }

    return false;
}

static bool xi_ui_acquire_active ( uint64_t id, uint32_t sub_id ) {
    if ( xi_ui_state->active_id == 0 && !xi_ui_state->cleared_active ) {
        xi_ui_state->active_id = id;
        xi_ui_state->active_sub_id = sub_id;
        xi_ui_state->keyboard_id = 0;
        return true;
    }

    return false;
}

static bool xi_ui_acquire_focus ( uint64_t id, uint32_t sub_id ) {
    xi_ui_state->focused_id = id;
    xi_ui_state->focused_sub_id = sub_id;
    xi_ui_state->focus_time = 0;
    return true;
}

static bool xi_ui_release_focus ( uint64_t id, uint32_t sub_id ) {
    if ( xi_ui_state->focused_id == id && xi_ui_state->focused_sub_id == sub_id ) {
        xi_ui_state->focused_id = 0;
        xi_ui_state->focused_sub_id = 0;
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
        xi_ui_state->keyboard_id = 0;

        return true;
    }

    return false;
}

// TODO is this needed?
static bool xi_ui_release_hovered ( uint64_t id ) {
    return false;
    if ( xi_ui_state->hovered_id == id ) {
        xi_ui_state->hovered_id = 0;
        xi_ui_state->hovered_sub_id = 0;
        xi_ui_state->cleared_hovered = true;

        return true;
    }

    return false;
}

static void xi_ui_draw_rect_textured ( xi_workload_h workload, xi_color_t color, int32_t x, int32_t y, uint32_t width, uint32_t height, uint64_t sort_order, xg_texture_h texture ) {
    xi_draw_rect_t rect = xi_draw_rect_m (
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .color = color,
        .sort_order = sort_order,
        .scissor = xi_ui_state->active_scissor,
        .texture = texture,
        .uv0[0] = 0,
        .uv0[1] = 0,
        .uv1[0] = 1,
        .uv1[1] = 1,
    );
    xi_workload_cmd_draw ( workload, &rect, 1 );
}

static void xi_ui_draw_rect ( xi_workload_h workload, xi_color_t color, int32_t x, int32_t y, uint32_t width, uint32_t height, uint64_t sort_order ) {
    xi_draw_rect_t rect = xi_draw_rect_m (
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .color = color,
        .sort_order = sort_order,
        .scissor = xi_ui_state->active_scissor,
    );
    xi_workload_cmd_draw ( workload, &rect, 1 );
}

static void xi_ui_draw_tri ( xi_workload_h workload, xi_color_t color, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint64_t sort_order ) {
    xi_draw_tri_t tri = xi_default_draw_tri_m;
    tri.xy0[0] = x0;
    tri.xy0[1] = y0;
    tri.xy1[0] = x1;
    tri.xy1[1] = y1;
    tri.xy2[0] = x2;
    tri.xy2[1] = y2;
    tri.color = color;
    tri.sort_order = sort_order;
    tri.scissor = xi_ui_state->active_scissor;
    xi_workload_cmd_draw_tri ( workload, &tri, 1 );
}

static uint32_t xi_ui_draw_string ( xi_workload_h workload, xi_font_h font, xi_color_t color, const char* text, uint32_t x, uint32_t y, uint64_t sort_order ) {
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

        xi_draw_rect_t rect = xi_draw_rect_m();
        //rect.x = x + x_offset;
        //rect.y = y + ( font_info.pixel_height - box.height ) * scale;
        //rect.width = box.width * scale;
        //rect.height = box.height * scale;
        rect.x = box.xy0[0];
        rect.y = box.xy0[1];
        rect.width = box.xy1[0] - box.xy0[0];
        rect.height = box.xy1[1] - box.xy0[1];
        rect.color = color;
        rect.texture = xi_font_atlas_get ( font );
        rect.linear_sampler_filter = false;
        rect.uv0[0] = box.uv0[0];
        rect.uv0[1] = box.uv0[1];
        rect.uv1[0] = box.uv1[0];
        rect.uv1[1] = box.uv1[1];
        rect.sort_order = sort_order;
        rect.scissor = xi_ui_state->active_scissor;

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

void xi_ui_update_begin ( const wm_window_info_t* window_info, const wm_input_state_t* input_state, const wm_input_buffer_t* input_buffer, const rv_view_info_t* view_info ) {
    //xi_ui_state->base_x = -window_info->x - window_info->border_left;
    //xi_ui_state->base_y = -window_info->y - window_info->border_top;
    //xi_ui_state->base_width = window_info->width;
    //xi_ui_state->base_height = window_info->height;
    // Update
    std_tick_t now = std_tick_now();
    uint64_t delta = now - xi_ui_state->update.current_tick;

    xi_ui_state->update.os_window_width = window_info->width;
    xi_ui_state->update.os_window_height = window_info->height;
    xi_ui_state->update.os_window_handle = window_info->os_handle;

    xi_ui_state->update.current_tick = now;

    xi_ui_state->update.mouse_delta_x = ( int64_t ) input_state->cursor_x - ( int64_t ) xi_ui_state->update.input_state.cursor_x;
    xi_ui_state->update.mouse_delta_y = ( int64_t ) input_state->cursor_y - ( int64_t ) xi_ui_state->update.input_state.cursor_y;

    if ( input_state->mouse[wm_mouse_state_left_m] && !xi_ui_state->update.mouse_down ) {
        xi_ui_state->update.mouse_down_tick = now;
    }

    xi_ui_state->update.mouse_down = input_state->mouse[wm_mouse_state_left_m];

    xi_ui_state->update.wheel_up = input_state->mouse[wm_mouse_state_wheel_up_m];
    xi_ui_state->update.wheel_down = input_state->mouse[wm_mouse_state_wheel_down_m];

    xi_ui_state->update.input_state = *input_state;
    xi_ui_state->update.input_buffer = *input_buffer;

    if ( view_info ) {
        xi_ui_state->update.view_info = *view_info;
        xi_ui_state->update.valid_view_info = true;
    } else {
        xi_ui_state->update.valid_view_info = false;
    }

    // Internal
    xi_ui_state->hovered_id = 0;

    if ( !xi_ui_state->update.mouse_down ) {
        xi_ui_state->active_layer = 0;
    }

    xi_ui_state->hovered_layer = 0;

    xi_ui_state->cleared_hovered = false;
    xi_ui_state->cleared_active = false;

    xi_ui_state->in_section = false;
    xi_ui_state->minimized_section = false;

    if ( xi_ui_state->focused_id != 0 && !xi_ui_state->focus_stack_change ) {
        xi_ui_state->focus_time += delta;
    }

    xi_ui_state->focus_stack_change = false;
    xi_ui_state->focus_stack_count = 0;

    xi_ui_state->active_scissor = xi_null_scissor_m;
}

static uint32_t xi_ui_get_focus_stack_idx ( uint64_t id, uint32_t sub_id ) {
    for ( uint32_t i = 0; i < xi_ui_state->focus_stack_count; ++i ) {
        if ( xi_ui_state->focus_stack_ids[i] == id && xi_ui_state->focus_stack_sub_ids[i] == sub_id ) {
            return i;
        }
    }
    return -1;
}

void xi_ui_update_end ( void ) {
    if ( xi_ui_state->update.input_buffer.count > 0 && xi_ui_state->focus_stack_count > 0 ) {
        wm_input_event_t* event = &xi_ui_state->update.input_buffer.events[0];
        if ( event->type == wm_event_key_down_m ) {
            wm_keyboard_event_args_t args = event->args.keyboard;
            if ( args.keycode == wm_keyboard_state_tab_m ) {
                uint32_t idx = xi_ui_get_focus_stack_idx ( xi_ui_state->focused_id, xi_ui_state->focused_sub_id );
                if ( args.flags & wm_input_flag_bits_shift_m ) {
                    idx = ( idx - 1 ) % xi_ui_state->focus_stack_count;
                } else {
                    idx = ( idx + 1 ) % xi_ui_state->focus_stack_count;
                }
                xi_ui_acquire_focus ( xi_ui_state->focus_stack_ids[idx], xi_ui_state->focus_stack_sub_ids[idx] );
                xi_ui_state->focus_stack_change = true;
            }
        }
    }
}

xi_style_t xi_ui_inherit_style ( const xi_style_t* style ) {
    if ( xi_ui_state->layer_count == 0 ) {
        return *style;
    }

    xi_style_t* parent = &xi_ui_state->layers[xi_ui_state->layer_count - 1].style;
    xi_style_t result = xi_style_m();

    result.font = style->font != xi_null_handle_m ? style->font : parent->font;
    result.font_height = style->font_height != 0 ? style->font_height : parent->font_height;
    result.color = style->color.u32 != xi_color_invalid_m.u32 ? style->color : parent->color;
    result.font_color = style->font_color.u32 != xi_color_invalid_m.u32 ? style->font_color : parent->font_color;
    result.horizontal_alignment = style->horizontal_alignment != xi_horizontal_alignment_invalid_m ? style->horizontal_alignment : parent->horizontal_alignment;
    result.vertical_alignment = style->vertical_alignment != xi_vertical_alignment_invalid_m ? style->vertical_alignment : parent->vertical_alignment;
    result.horizontal_border_margin = style->horizontal_border_margin != xi_style_border_margin_invalid_m ? style->horizontal_border_margin : parent->horizontal_border_margin;
    result.horizontal_margin = style->horizontal_margin != xi_style_margin_invalid_m ? style->horizontal_margin : parent->horizontal_margin;
    result.horizontal_padding = style->horizontal_padding != xi_style_padding_invalid_m ? style->horizontal_padding : parent->horizontal_padding;

    return result;
}

void xi_ui_window_begin ( xi_workload_h workload, xi_window_state_t* state ) {
    xi_style_t style = xi_ui_inherit_style ( &state->style );

    xi_font_info_t font_info;
    xi_font_get_info ( &font_info, style.font );

    uint32_t title_pad_x = 10;
    uint32_t title_pad_y = 0;
    uint32_t header_height = std_max_u32 ( state->header_height, font_info.pixel_height ) + title_pad_y * 2;

    uint32_t x = 0, y = 0;
    uint32_t triangle_width = header_height  * 0.8;
    uint32_t triangle_height = header_height * 0.8;

    uint32_t window_height = state->minimized ? header_height : state->height;
    uint32_t window_width = state->width;
    uint32_t scrollbar_width = state->scrollable ? 10 : 0;

    xi_ui_layer_t* layer = xi_ui_layer_add ( state->x, state->y, window_width, window_height, 0, 0, &style, state->id );
    layer = xi_ui_layer_add ( 0, 0, window_width - scrollbar_width, window_height, state->padding_x, state->padding_y, &style, state->id );
    layer->line_y = header_height;

    // corner and scrollbar sizes
    uint32_t corner_size = 15;
    uint32_t scrollbar_height = layer->height - header_height - corner_size;
    uint32_t scroll_handle_height = 30;
    float scroll = state->scroll;
    uint32_t title_size = xi_ui_string_width ( state->title, style.font ) + title_pad_x * 2 + triangle_width;

    // acquire active
    // 0: header
    // 1: resize triangle
    // 2: minimize triangle
    // 3: scrollbar
    if ( xi_ui_layer_cursor_test ( x + title_pad_x, y + title_pad_y, triangle_width, triangle_height ) ) {
        xi_ui_acquire_hovered ( state->id, 2 );

        if ( xi_ui_cursor_click() ) {
            xi_ui_acquire_active ( state->id, 2 );
        }
    } else if ( xi_ui_layer_cursor_test ( x, y, window_width, header_height ) ) {
        xi_ui_acquire_hovered ( state->id, 0 );

        if ( xi_ui_cursor_click() ) {
            xi_ui_acquire_active ( state->id, 0 );
        }
    } else if ( xi_ui_layer_cursor_test ( window_width - 20, layer->height - 20, 20, 20 ) ) {
        if ( xi_ui_cursor_click() ) {
            xi_ui_acquire_active ( state->id, 1 );
        }
    } else if ( state->scrollable && xi_ui_layer_cursor_test ( window_width - scrollbar_width, header_height + ( scrollbar_height - scroll_handle_height ) * scroll , scrollbar_width, scroll_handle_height ) ) {
        if ( xi_ui_cursor_click() ) {
            xi_ui_acquire_active ( state->id, 3 );
        }
    } else {
        xi_ui_release_hovered ( state->id );
    }

    // drag
    if ( xi_ui_state->active_id == state->id && xi_ui_state->active_sub_id == 0 ) {
        //float delta = std_tick_to_milli_f32 ( xi_ui_state.current_tick - xi_ui_state.mouse_down_tick );

        //if ( delta > 100.f ) {
        state->x += xi_ui_state->update.mouse_delta_x;
        state->y += xi_ui_state->update.mouse_delta_y;
        //}

        if ( state->x < 0 ) {
            state->x = 0;
        }

        if ( state->x + state->width > xi_ui_state->update.os_window_width ) {
            state->x = xi_ui_state->update.os_window_width - state->width;
        }

        if ( state->y < 0 ) {
            state->y = 0;
        }
    }

    if ( state->y + window_height > xi_ui_state->update.os_window_height ) {
        state->y = xi_ui_state->update.os_window_height - window_height;
    }

    // scroll
    if ( state->scrollable ) {
        uint64_t* lookup = std_hash_map_lookup ( &xi_ui_state->windows_map, state->id );
        // stabilize scroll
        // if total window content height changed from prev, fit the scroll to keep the window view steady
        if ( lookup ) {
            uint32_t prev_height = ( *lookup ) & 0xffffffff;
            uint32_t prev_prev_height = ( *lookup ) >> 32;
            if ( prev_height != prev_prev_height && prev_prev_height != 0 ) {
                scroll = scroll * prev_prev_height / prev_height;
            }
        }

        // update scroll bar
        if ( xi_ui_state->active_id == state->id && xi_ui_state->active_sub_id == 3 ) {
            int32_t handle_y = ( scrollbar_height - scroll_handle_height ) * scroll;
            handle_y += xi_ui_state->update.mouse_delta_y;
            handle_y = std_max_i32 ( handle_y, 0 );
            scroll = ( float ) handle_y / ( scrollbar_height - scroll_handle_height );
        }

        // update scroll
        if ( lookup ) {
            uint32_t prev_height = ( *lookup ) & 0xffffffff;
            float tick = ( float ) ( window_height - header_height ) / prev_height;
            tick *= 0.2f;
            if ( xi_ui_layer_cursor_test ( 0, 0, window_width, layer->height ) ) {
                if ( xi_ui_state->update.wheel_up ) {
                    scroll -= tick;
                }

                if ( xi_ui_state->update.wheel_down ) {
                    scroll += tick;
                }
            }
            scroll = std_min_f32 ( std_max_f32 ( scroll, 0 ), 1 );
            state->scroll = scroll;
            layer->line_y -= prev_height * scroll;
        }
    }

    // resize
    if ( xi_ui_state->active_id == state->id && xi_ui_state->active_sub_id == 1 ) {
        //float delta = std_tick_to_milli_f32 ( xi_ui_state.current_tick - xi_ui_state.mouse_down_tick );

        //if ( delta > 100.f ) {
        //if ( xi_ui_state->input_state.cursor_x + xi_ui_state->base_x > layer->x + title_pad_x || state->width > title_pad_x + 10 ) {
        int32_t width = state->width;
        int32_t height = state->height;

        int32_t min_width = title_size;
        int32_t min_height = header_height + corner_size + scroll_handle_height;

        if ( width > min_width ) {
            width += xi_ui_state->update.mouse_delta_x;
        } else {
            if ( xi_ui_state->update.input_state.cursor_x > state->x + min_width ) {
                uint32_t delta = xi_ui_state->update.input_state.cursor_x - ( state->x + min_width );
                width += delta;
            }
        }

        if ( height > min_height ) {
            height += xi_ui_state->update.mouse_delta_y;
        } else {
            if ( xi_ui_state->update.input_state.cursor_y > state->y + min_height ) {
                uint32_t delta = xi_ui_state->update.input_state.cursor_y - ( state->y + min_height );
                height += delta;
            }
        }

        if ( state->x + width > xi_ui_state->update.os_window_width ) {
            width = xi_ui_state->update.os_window_width - state->x;
        }

        if ( state->y + height > xi_ui_state->update.os_window_height ) {
            height = xi_ui_state->update.os_window_height - state->y;
        }

        if ( height < header_height + corner_size ) {
            height = header_height + corner_size;
        }

        if ( width < ( int32_t ) title_pad_x + 10 ) {
            width = title_pad_x + 10;
        }

        width = std_max_i32 ( width, min_width );
        height = std_max_i32 ( height, min_height );

        state->width = width;
        state->height = height;

        //}
    }

    // minimize
    bool minimize_pressed = false;

    if ( !xi_ui_state->update.mouse_down && xi_ui_state->active_id == state->id && xi_ui_state->active_sub_id == 2 ) {
        if ( xi_ui_state->hovered_id == state->id ) {
            minimize_pressed = true;
        }

        xi_ui_release_active ( state->id );
    }

    if ( minimize_pressed ) {
        state->minimized = !state->minimized;
    }

    xi_ui_state->minimized_window = state->minimized;

    // release active
    if ( xi_ui_state->active_id == state->id && !xi_ui_state->update.mouse_down ) {
        xi_ui_release_active ( state->id );
    }

    // draw
    //  base background
    if ( !state->minimized ) {
        xi_ui_draw_rect ( workload, style.color, layer->x, layer->y, window_width, window_height, state->sort_order );
    }

    //  title bar
    xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style.color, 0.3 ), layer->x, layer->y, window_width, header_height, state->sort_order );

    // title triangle
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
    xi_ui_draw_string ( workload, style.font, style.font_color, state->title, layer->x + title_pad_x * 2 + triangle_width, layer->y + y + title_pad_y / 2 + ( header_height - font_info.pixel_height ) / 2, state->sort_order );

    // scroll bar
    if ( state->scrollable && !state->minimized ) {
        xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style.color, 0.5 ), 
            layer->x + window_width - scrollbar_width, 
            layer->y + header_height, 
            scrollbar_width, scrollbar_height, 
            state->sort_order );
        xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style.color, 0.2 ), 
            layer->x + window_width - scrollbar_width, 
            layer->y + header_height + ( scrollbar_height - scroll_handle_height ) * scroll, 
            scrollbar_width, scroll_handle_height, 
            state->sort_order );
    }

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
    if ( !state->minimized ) {
        xi_ui_draw_tri ( workload, xi_color_rgb_mul_m ( style.color, 0.5 ),
            layer->x + window_width, layer->y + layer->height - corner_size,
            layer->x + window_width, layer->y + layer->height,
            layer->x + window_width - corner_size, layer->y + layer->height,
            state->sort_order  );
    }

    xi_ui_state->window_id = state->id;

    xi_scissor_h scissor = xi_workload_scissor ( workload, state->x, state->y + header_height, state->width, state->height - header_height );
    xi_ui_state->active_scissor = scissor;
}

void xi_ui_window_end ( xi_workload_h workload ) {
    std_unused_m ( workload );

    uint64_t* lookup = std_hash_map_lookup_insert ( &xi_ui_state->windows_map, xi_ui_state->window_id, NULL );
    xi_ui_layer_t* layer = &xi_ui_state->layers[0];
    uint64_t prev_total_content_heigh = ( *lookup ) & 0xffffffff;
    *lookup = layer->total_content_height | ( prev_total_content_heigh << 32 );

    xi_ui_layer_pop_all();
    xi_ui_state->active_scissor = xi_null_scissor_m;
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
        xi_ui_state->culled_section = true;
        return;
    }

    xi_ui_state->culled_section = false;

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

    if ( !xi_ui_state->update.mouse_down && xi_ui_state->active_id == state->id ) {
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
    if ( layer->line_y >= 0 ) {
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
        xi_ui_draw_string ( workload, style.font, style.font_color, state->title, layer->x + x + title_pad_x * 2 + triangle_width, layer->y + y + title_pad_y / 2 + ( header_height - font_info.pixel_height ) / 2, state->sort_order );
    }

    // new layer
    //xi_ui_layer_add ( x + title_pad_x, layer->line_y, layer->width, layer->height, layer->line_padding_x, layer->line_padding_y, &style );
}

void xi_ui_section_end ( xi_workload_h workload ) {
    std_unused_m ( workload );

    xi_ui_state->culled_section = false;

    if ( !xi_ui_state->in_section ) {
        return;
    }

    //xi_ui_layer_t* layer = &xi_ui_state->layers[xi_ui_state->layer_count - 1];
    //uint32_t title_pad_x = 10;
    //layer->x -= title_pad_x;
    //layer->width += title_pad_x;
    //layer->line_height = 4;

    if ( !xi_ui_state->minimized_section ) {
        xi_ui_newline();
        xi_ui_layer_t* layer = &xi_ui_state->layers[xi_ui_state->layer_count - 1];
        layer->line_y += layer->line_padding_y * 2;
        xi_ui_layer_add_content ( layer->line_padding_y * 2 );
    }

    xi_ui_state->in_section = false;
    xi_ui_state->minimized_section = false;
}

void xi_ui_label ( xi_workload_h workload, xi_label_state_t* state ) {
    xi_style_t style = xi_ui_inherit_style ( &state->style );

    xi_font_info_t font_info;
    xi_font_get_info ( &font_info, style.font );

    int32_t x, y;
    uint32_t width = xi_ui_string_width ( state->text, style.font );
    uint32_t height = std_max_u32 ( state->height, font_info.pixel_height );

    if ( !xi_ui_layer_add_element ( &x, &y, width, height, &style ) ) {
        return;
    }

    xi_ui_draw_string ( workload, style.font, style.font_color, state->text, x, y, state->sort_order );

    if ( xi_ui_cursor_test ( x, y, width, height ) ) {
        xi_ui_acquire_hovered ( state->id, 0 );
    } else {
        xi_ui_release_hovered ( state->id );
    }
}

bool xi_ui_textfield_internal ( xi_workload_h workload, xi_textfield_state_t* state, uint32_t sub_id ) {
    xi_style_t style =  xi_ui_inherit_style ( &state->style );

    xi_font_info_t font_info;
    xi_font_get_info ( &font_info, style.font );

    uint32_t text_width = xi_ui_string_width ( state->text, style.font );
    uint32_t width = std_max_u32 ( text_width, state->width );
    uint32_t height = std_max_u32 ( font_info.pixel_height, state->height );

    int32_t x, y;

    if ( !xi_ui_layer_add_element ( &x, &y, width, height, &style ) ) {
        return false;
    }

    bool result = false;

    xi_ui_focus_stack_push ( state->id, sub_id );

    if ( xi_ui_cursor_test ( x, y, width, height ) ) {
        xi_ui_acquire_hovered ( state->id, 0 );

        if ( xi_ui_cursor_click() ) {
            xi_ui_acquire_active ( state->id, sub_id );
            xi_ui_acquire_focus ( state->id, sub_id );
        }
    } else {
        xi_ui_release_hovered ( state->id );

        if ( xi_ui_cursor_click() ) {
            xi_ui_release_focus ( state->id, sub_id );
        }
    }

    if ( xi_ui_state->active_id == state->id && !xi_ui_state->update.mouse_down ) {
        xi_ui_release_active ( state->id );
    }

    // string update
    //bool changed = false;
    char buffer[xi_textfield_text_size_m + 1];
    std_stack_t stack = std_static_stack_m ( buffer );
    std_stack_string_append ( &stack, state->text );

    bool has_focus = ( xi_ui_state->focused_id == state->id && xi_ui_state->focused_sub_id == sub_id );

    if ( has_focus ) {
        // text edit
        for ( uint32_t i = 0; i < xi_ui_state->update.input_buffer.count; ++i ) {
            wm_input_event_t* event = &xi_ui_state->update.input_buffer.events[i];

            if ( event->type == wm_event_key_down_m ) {
                wm_keyboard_event_args_t args = event->args.keyboard;

                if ( args.keycode == wm_keyboard_state_backspace_m ) {
                    std_stack_string_pop ( &stack );
                } else if ( args.keycode == wm_keyboard_state_enter_m ) {
                    //changed = false;
                    result = true;
                    xi_ui_release_focus ( state->id, sub_id );
                    break;
                } else if ( args.keycode == wm_keyboard_state_tab_m ) {
                    //changed = false;
                    result = true;
                    break;
                } else {
                    if ( args.keycode >= wm_keyboard_state_a_m && args.keycode <= wm_keyboard_state_space_m ) {
                        std_stack_string_append_char ( &stack, args.character );
                    }
                }

                //changed = true;
            }
        }

        //if ( changed ) {
        std_str_copy_static_m ( state->text, buffer );
        //}

        // blinking cursor
        if ( ! ( ( ( uint64_t ) ( std_tick_to_milli_f32 ( xi_ui_state->focus_time ) ) >> 9 ) & 0x1 ) ) {
            std_stack_string_append ( &stack, "_" );
        }
    }

    // draw
    float color_scale = xi_ui_state->keyboard_id == state->id ? 0.35f : 0.5f;
    xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style.color, color_scale ), x, y, width, height, state->sort_order );
    uint32_t string_offset = 0;
    if ( state->text_alignment == xi_horizontal_alignment_centered_m ) {
        string_offset = ( width - text_width ) / 2;
    } else if ( state->text_alignment == xi_horizontal_alignment_right_to_left_m ) {
        string_offset = width - ( text_width + style.font_height ); // add some space for the trailing _
    }
    xi_ui_draw_string ( workload, style.font, style.font_color, buffer, x + string_offset, y, state->sort_order );

    return result;
}

bool xi_ui_textfield ( xi_workload_h workload, xi_textfield_state_t* state ) {
    return xi_ui_textfield_internal ( workload, state, 0 );
}

bool xi_ui_switch ( xi_workload_h workload, xi_switch_state_t* state ) {
    xi_style_t style = xi_ui_inherit_style ( &state->style );

    uint32_t top_margin = 2;
    uint32_t padding = 2;
    uint32_t inner_padding = 1;
    int32_t x, y;

    //xi_ui_layer_t* layer = &xi_ui_state->layers[xi_ui_state->layer_count - 1];
    //std_log_info_m ( std_fmt_i64_m, layer->delta_y );

    if ( !xi_ui_layer_add_element ( &x, &y, state->width, state->height + top_margin, &style ) ) {
        return false;
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
    bool changed = false;
    // TODO change value on mouse release (!xi_ui_state->mouse_down seems to cause issues of non-registered clicks?)
    if ( xi_ui_state->active_id == state->id && xi_ui_state->update.mouse_down ) {
        if ( xi_ui_state->hovered_id == state->id ) {
            state->value = ! ( state->value );
            changed = true;
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
    return changed;
}

bool xi_ui_arrow ( xi_workload_h workload, xi_arrow_state_t* state ) {
    xi_style_t style = xi_ui_inherit_style ( &state->style );

    int32_t x, y;
    uint32_t width = state->width;
    uint32_t height = state->height;

    if ( !xi_ui_layer_add_element ( &x, &y, width, height, &style ) ) {
        return false;
    }

    if ( xi_ui_cursor_test ( x, y, width, height ) ) {
        xi_ui_acquire_hovered ( state->id, 0 );

        if ( xi_ui_cursor_click() ) {
            xi_ui_acquire_active ( state->id, 0 );
        }
    } else {
        xi_ui_release_hovered ( state->id );
    }

    // state update
    bool changed = false;
    if ( xi_ui_state->active_id == state->id && xi_ui_state->update.mouse_down ) {
        if ( xi_ui_state->hovered_id == state->id ) {
            state->expanded = ! ( state->expanded );
            changed = true;
        }

        xi_ui_release_active ( state->id );
    }

    if ( state->expanded ) {
        xi_ui_draw_tri ( workload, xi_color_rgb_mul_m ( style.color, 0.5 ),
            x,                  y,
            x + width,          y,
            x + width / 2,      y + height,
            state->sort_order );
    } else {
        xi_ui_draw_tri ( workload, xi_color_rgb_mul_m ( style.color, 0.5 ),
            x,                  y,
            x + width,          y + height / 2,
            x,                  y + height,
            state->sort_order );
    }

    return changed;
}

void xi_ui_slider ( xi_workload_h workload, xi_slider_state_t* state ) {
    xi_style_t style = xi_ui_inherit_style ( &state->style );

    uint32_t padding = 2;
    int32_t x, y;

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
        int32_t value = xi_ui_state->update.input_state.cursor_x - x - padding - handle_width / 2;

        if ( value < 0 ) {
            value = 0;
        }

        if ( value > range ) {
            value = range;
        }

        state->value = ( float ) value / range;

        if ( !xi_ui_state->update.mouse_down ) {
            xi_ui_release_active ( state->id );
        }
    }

    // Draw
    uint32_t x_offset = range * state->value + padding;
    xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style.color, 0.5f ), x, y, state->width, state->height, state->sort_order );
    xi_ui_draw_rect ( workload, style.color, x + x_offset, y + padding, handle_width, state->height - padding * 2, state->sort_order );
}

void xi_ui_texture ( xi_workload_h workload, xi_texture_state_t* state ) {
    xi_style_t style = xi_ui_inherit_style ( &state->style );

    uint32_t width = state->width;
    uint32_t height = state->height;

    int32_t x, y;
    if ( !xi_ui_layer_add_element (&x, &y, width, height, &style ) ) {
        return;
    }

    xi_ui_draw_rect_textured ( workload, xi_color_rgba_u32_m ( 255, 255, 255, 255 ), x, y, width, height, state->sort_order, state->handle );
}

void xi_ui_overlay_texture ( xi_workload_h workload, xi_overlay_texture_state_t* state ) {
    uint32_t width = xi_ui_state->update.os_window_width;
    uint32_t height = xi_ui_state->update.os_window_height;
    xi_scissor_h active_scissor = xi_ui_state->active_scissor;
    xi_ui_state->active_scissor = xi_null_scissor_m;
    // TODO sort order as param?
    xi_ui_draw_rect_textured ( workload, xi_color_rgba_u32_m ( 255, 255, 255, 255 ), 0, 0, width, height, 0, state->handle );
    xi_ui_state->active_scissor = active_scissor;
}

bool xi_ui_button ( xi_workload_h workload, xi_button_state_t* state ) {
    xi_style_t style =  xi_ui_inherit_style ( &state->style );

    xi_font_info_t font_info;
    xi_font_get_info ( &font_info, style.font );

    uint32_t text_width = xi_ui_string_width ( state->text, style.font );
    uint32_t width = std_max_u32 ( text_width, state->width );
    width += 2 * style.horizontal_padding;
    uint32_t height = std_max_u32 ( font_info.pixel_height, state->height );

    int32_t x, y;

    if ( !xi_ui_layer_add_element ( &x, &y, width, height, &style ) ) {
        return false;
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

    if ( !xi_ui_state->update.mouse_down && xi_ui_state->active_id == state->id ) {
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
    float color_scale = xi_ui_state->active_id == state->id ? 0.35f : 0.5f;
    xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style.color, color_scale ), x, y, width, height, state->sort_order );
    xi_ui_draw_string ( workload, style.font, style.font_color, state->text, x + ( width - text_width ) / 2, y, state->sort_order );

    return pressed;
}

bool xi_ui_select ( xi_workload_h workload, xi_select_state_t* state ) {
    xi_style_t style = xi_ui_inherit_style ( &state->style );

    xi_font_info_t font_info;
    xi_font_get_info ( &font_info, style.font );

    uint32_t selected_text_width = xi_ui_string_width ( state->items[state->item_idx], style.font );
    uint32_t width = std_max_u32 ( selected_text_width, state->width );
    uint32_t height = std_max_u32 ( font_info.pixel_height, state->height );

    int32_t x, y;

    if ( !xi_ui_layer_add_element ( &x, &y, width, height, &style ) ) {
        return false;
    }

    bool selected = false;
    if ( xi_ui_state->active_id == state->id ) {
        if ( xi_ui_cursor_test ( x, y, width, height * ( state->item_count + 1 ) ) ) {
            xi_ui_acquire_hovered ( state->id, 0 );

            if ( xi_ui_cursor_click() ) {
                uint32_t y_offset = xi_ui_state->update.input_state.cursor_y - y;

                if ( y_offset > height ) {
                    state->item_idx = y_offset / height - 1;
                    selected = true;
                }

                xi_ui_release_active ( state->id );
            }
        } else {
            xi_ui_release_hovered ( state->id );

            if ( xi_ui_state->update.mouse_down ) {
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
    xi_ui_draw_string ( workload, style.font, style.font_color, state->items[state->item_idx], x + ( width - text_width ) / 2, y + ( height - font_info.pixel_height ) / 3, state->sort_order );

    if ( xi_ui_state->active_id == state->id ) {
        xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style.color, 0.5f ), x, y + height, width, height * state->item_count, state->sort_order );

        // TODO use this to highlight hover
        //xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style->color, 0.75f ), layer->x + x, layer->y + y + state->height * ( state->item_idx + 1 ), state->width, state->height, state->sort_order );

        for ( uint32_t i = 0; i < state->item_count; ++i ) {
            if ( xi_ui_cursor_test ( x, y + height * ( i + 1 ), width, height ) ) {
                xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style.color, 0.75f ), x, y + height * ( i + 1 ), width, height, state->sort_order );
                //xi_ui_draw_rect ( workload, xi_color_rgb_mul_m ( style->color, 0.75f ), layer->x + x, layer->y + y + height * ( i + 1 ), width, height, state->sort_order );
            }

            uint32_t text_width = xi_ui_string_width ( state->items[i], style.font );
            xi_ui_draw_string ( workload, style.font, style.font_color, state->items[i], x + ( width - text_width ) / 2, y + height * ( i + 1 ), state->sort_order );
        }
    }

    return selected;
}

uint64_t xi_ui_get_active_id ( void ) {
    return xi_ui_state->active_id ? xi_ui_state->active_id : xi_ui_state->active_layer;
}

uint64_t xi_ui_get_hovered_id ( void ) {
    return xi_ui_state->hovered_id ? xi_ui_state->hovered_id : xi_ui_state->hovered_layer;
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

static bool xi_ui_property_editor_bool ( xi_workload_h workload, xi_property_editor_state_t* state, uint32_t data_offset, uint64_t xi_id, uint64_t sub_id ) {
    bool value = * ( bool* ) ( state->data + data_offset );
    xi_switch_state_t switch_state = xi_switch_state_m (
        .width = state->property_width,
        .height = state->property_height,
        .id = state->id,
        .sort_order = state->sort_order,
        .style = state->style,
        .value = value
    );
    std_unused_m ( sub_id );

    bool changed = xi_ui_switch ( workload, &switch_state );

    if ( changed ) {
        * ( bool* ) ( state->data + data_offset ) = switch_state.value;
    }
    return changed;
}

static bool xi_ui_property_editor_f32 ( xi_workload_h workload, xi_property_editor_state_t* state, uint32_t data_offset, uint64_t xi_id, uint64_t sub_id ) {
    xi_textfield_state_t textfield = xi_textfield_state_m (
        .font = state->font,
        .width = state->property_width,
        .height = state->property_height,
        .id = state->id,// ^ xi_id,
        .sort_order = state->sort_order,
        .style = state->style,
    );

    //bool had_focus = xi_ui_state->focused_id == state->id && xi_ui_state->focused_sub_id == sub_id && !xi_ui_state->focus_stack_change;

    bool had_focus = ( xi_ui_state->focused_id == state->id && xi_ui_state->focused_sub_id == sub_id && xi_ui_state->focus_time > 0 );

    if ( had_focus ) {
        std_str_copy_static_m ( textfield.text, xi_ui_state->property_editor_buffer );
    } else {
        std_f32_to_str ( *( float* ) ( state->data + data_offset ), textfield.text, xi_textfield_text_size_m );
    }
    
    bool enter = xi_ui_textfield_internal ( workload, &textfield, sub_id );

    bool has_focus = xi_ui_state->focused_id == state->id && xi_ui_state->focused_sub_id == sub_id;// && xi_ui_state->focus_stack_prev_id == 0;
    
    bool result = false;

    //if ( !had_focus && has_focus ) 
    //    std_log_info_m ( "hit");

    //if (xi_ui_state->focus_stack_prev_id != 0)
    //    std_log_info_m ( std_fmt_u64_m, xi_ui_state->focus_stack_prev_id );

#if 0
    if ( had_focus ) {
        if ( has_focus ) {
            if ( enter ) {
                float f32 = std_str_to_f32 ( textfield.text );
                *( float* ) ( state->data + data_offset ) = f32;
                result = true;
                xi_ui_release_focus();
            } else {
                std_str_copy_static_m ( xi_ui_state->property_editor_buffer, textfield.text );
            }
        } else {
            float f32 = std_str_to_f32 ( textfield.text );
            *( float* ) ( state->data + data_offset ) = f32;
            result = true;
        }
    } else {
        if ( has_focus ) {
                std_log_info_m ( "hit " std_fmt_u64_m, sub_id );
            if ( enter ) {
                float f32 = std_str_to_f32 ( textfield.text );
                *( float* ) ( state->data + data_offset ) = f32;
                result = true;
            } else {
                std_str_copy_static_m ( xi_ui_state->property_editor_buffer, textfield.text );
            }
        }
    }
#else
    if ( had_focus && enter ) {
        float f32 = std_str_to_f32 ( textfield.text );
        *( float* ) ( state->data + data_offset ) = f32;
        result = true;
    } else if ( has_focus ) {
        std_str_copy_static_m ( xi_ui_state->property_editor_buffer, textfield.text );
    }
#endif

    return result;
}

bool xi_ui_property_editor ( xi_workload_h workload, xi_property_editor_state_t* state ) {
    bool edited = false;
    if ( state->type == xi_property_4f32_m ) {
        edited |= xi_ui_property_editor_f32 ( workload, state, 12, xi_line_id_m(), 3 );
        edited |= xi_ui_property_editor_f32 ( workload, state, 8, xi_line_id_m(), 2 );
        edited |= xi_ui_property_editor_f32 ( workload, state, 4, xi_line_id_m(), 1 );
        edited |= xi_ui_property_editor_f32 ( workload, state, 0, xi_line_id_m(), 0 );
    } else if ( state->type == xi_property_3f32_m ) {
        edited |= xi_ui_property_editor_f32 ( workload, state, 8, xi_line_id_m(), 2 );
        edited |= xi_ui_property_editor_f32 ( workload, state, 4, xi_line_id_m(), 1 );
        edited |= xi_ui_property_editor_f32 ( workload, state, 0, xi_line_id_m(), 0 );
    } else if ( state->type == xi_property_f32_m ) {
        edited |= xi_ui_property_editor_f32 ( workload, state, 0, xi_line_id_m(), 0 );
    } else if ( state->type == xi_property_bool_m ) {
        edited |= xi_ui_property_editor_bool ( workload, state, 0, xi_line_id_m(), 0 );
    } else {
        std_not_implemented_m();
    }
    return edited;
}

void xi_ui_geo_init ( xg_device_h device_handle ) {
    xi_ui_state->device = device_handle;
    xi_ui_state->transform_geo.cpu = xg_geo_util_generate_sphere ( 0.1f, 30, 30 );
    xi_ui_state->transform_geo.gpu = xg_geo_util_upload_geometry_to_gpu ( device_handle, &xi_ui_state->transform_geo.cpu );
}

static bool xi_ui_ray_triangle_intersect ( sm_vec_3f_t* intersection, float* ray_depth,  sm_vec_3f_t ray_origin, sm_vec_3f_t ray_direction, sm_vec_3f_t tri_a, sm_vec_3f_t tri_b, sm_vec_3f_t tri_c ) {
    const float epsilon = 1e-5f;
    bool ret = false;
    
    sm_vec_3f_t e1 = sm_vec_3f_sub ( tri_b, tri_a );
    sm_vec_3f_t e2 = sm_vec_3f_sub ( tri_c, tri_a );
    sm_vec_3f_t h = sm_vec_3f_cross ( ray_direction, e2 );
    float a = sm_vec_3f_dot ( e1, h );

    if ( a > -epsilon && a < epsilon ) {
        goto exit;
    }

    float f = 1 / a;
    sm_vec_3f_t s = sm_vec_3f_sub ( ray_origin, tri_a );
    float u = f * ( sm_vec_3f_dot ( s, h ) );

    if ( u < 0.0 || u > 1.0 ) {
        goto exit;
    }

    sm_vec_3f_t q = sm_vec_3f_cross ( s, e1 );
    float v = f * sm_vec_3f_dot ( ray_direction, q );

    if ( v < 0.0 || u + v > 1.0 ) {
        goto exit;
    }

    float t = f * sm_vec_3f_dot ( e2, q );

    if ( t > epsilon ) {
        sm_vec_3f_t offset = sm_vec_3f_mul ( ray_direction, t );
        *intersection = sm_vec_3f_add ( offset, ray_origin );
        *ray_depth = t;

        ret = true;
        //goto exit;
    }

exit:
    return ret;
}

static bool xi_ui_ray_plane_intersect ( sm_vec_3f_t* intersection, float* ray_depth, sm_vec_3f_t ray_origin, sm_vec_3f_t ray_direction, sm_vec_4f_t plane ) {
    sm_vec_4f_t p = sm_vec_3f_to_4f ( ray_origin, 1 );
    sm_vec_4f_t v = sm_vec_3f_to_4f ( ray_direction, 0 );

    sm_vec_3f_t q = sm_vec_3f_sub ( ray_origin, sm_vec_3f_mul ( ray_direction, sm_vec_4f_dot ( plane, p ) / sm_vec_4f_dot ( plane, v ) ) );
    *intersection = q;

    // TODO properly handle all ray/plane cases
    return true;
}

typedef struct {
    float origin[3];
    float direction[3];
} xi_ui_camera_ray_t;

static bool xi_ui_mouse_camera_ray_build ( xi_ui_camera_ray_t* ray ) {
    if ( !xi_ui_state->update.valid_view_info ) {
        return false;
    }

    const rv_view_info_t* view = &xi_ui_state->update.view_info;
    const wm_input_state_t* input = &xi_ui_state->update.input_state;

    // [0:1], y points down
    float ndc_x = ( input->cursor_x + 0.5f ) / xi_ui_state->update.os_window_width;
    float ndc_y = ( input->cursor_y + 0.5f ) / xi_ui_state->update.os_window_height;
    // [-1:1], y points up
    float screen_x = 2 * ndc_x - 1;
    float screen_y = 1 - 2 * ndc_y;
    // [-tan(fov_y/2):tan(fov_y/2)], x scaled by aspect_ratio
    float frustum_x = screen_x * tanf ( view->proj_params.perspective.fov_y / 2 ) * view->proj_params.perspective.aspect_ratio;
    float frustum_y = screen_y * tanf ( view->proj_params.perspective.fov_y / 2 );
    sm_vec_3f_t dir = { frustum_x, frustum_y, 1 };
    dir = sm_vec_3f_norm ( dir );

    dir = sm_matrix_4x4f_transform_f3_dir ( sm_matrix_4x4f ( view->inverse_view_matrix.f ), dir );

    *ray = ( xi_ui_camera_ray_t ) {
        .origin = { view->transform.position[0], view->transform.position[1], view->transform.position[2] },
        .direction = { dir.x, dir.y, dir.z }
    };

    return true;
}

static bool xi_ui_transform_mouse_pick_test ( const xi_ui_camera_ray_t* ray, sm_mat_4x4f_t xform ) {
    xg_geo_util_geometry_data_t* geo = &xi_ui_state->transform_geo.cpu;

    for ( uint32_t i = 0; i < geo->index_count; i += 3 ) {
        sm_vec_3f_t pos;
        float depth;

        sm_vec_3f_t tri_a = { geo->pos[geo->idx[i + 0] + 0], geo->pos[geo->idx[i + 0] + 1], geo->pos[geo->idx[i + 0] + 2] };
        sm_vec_3f_t tri_b = { geo->pos[geo->idx[i + 1] + 0], geo->pos[geo->idx[i + 1] + 1], geo->pos[geo->idx[i + 1] + 2] };
        sm_vec_3f_t tri_c = { geo->pos[geo->idx[i + 2] + 0], geo->pos[geo->idx[i + 2] + 1], geo->pos[geo->idx[i + 2] + 2] };
        
        tri_a = sm_matrix_4x4f_transform_f3 ( xform, tri_a );
        tri_b = sm_matrix_4x4f_transform_f3 ( xform, tri_b );
        tri_c = sm_matrix_4x4f_transform_f3 ( xform, tri_c );

        //std_log_info_m ( std_fmt_f32_m" "std_fmt_f32_m" "std_fmt_f32_m, ray->direction[0], ray->direction[1], ray->direction[2] );

        bool intersects = xi_ui_ray_triangle_intersect ( &pos, &depth, sm_vec_3f ( ray->origin ), sm_vec_3f ( ray->direction ), tri_a, tri_b, tri_c );
        if ( intersects ) {
            return true;
        }
    }

    return false;
}
 
bool xi_ui_draw_transform ( xi_workload_h workload_handle, xi_transform_state_t* state ) {
    xg_geo_util_geometry_gpu_data_t* xform = &xi_ui_state->transform_geo.gpu;
    uint32_t idx_count = xi_ui_state->transform_geo.cpu.index_count;

    xi_ui_camera_ray_t ray;
    if ( !xi_ui_mouse_camera_ray_build ( &ray ) ) {
        return false;
    }

    sm_vec_3f_t p = sm_vec_3f ( state->position );
    sm_vec_3f_t cam_p = sm_vec_3f ( xi_ui_state->update.view_info.transform.position );
    float d = sm_vec_3f_len ( sm_vec_3f_sub ( p, cam_p ) );
    float fov_y = xi_ui_state->update.view_info.proj_params.perspective.fov_y;
    float pixel_size = 15;
    float scale = ( 2 * pixel_size * d ) / ( xi_ui_state->update.os_window_height * tan ( fov_y / 2 ) );

    sm_mat_4x4f_t trans = {
        .r0[0] = scale,
        .r1[1] = scale,
        .r2[2] = scale,
        .r3[3] = 1,
        .r0[3] = state->position[0],
        .r1[3] = state->position[1],
        .r2[3] = state->position[2],
    };

    bool hovered = false;
    if ( xi_ui_transform_mouse_pick_test ( &ray, trans ) ) {
        xi_ui_acquire_hovered ( state->id, 0 );
        hovered = true;

        if ( xi_ui_cursor_click() ) {
            xi_ui_acquire_active ( state->id, 0 );
        }
    } else {
        xi_ui_release_hovered ( state->id );
    }

    if ( !xi_ui_state->update.mouse_down && xi_ui_state->active_id == state->id ) {
        xi_ui_release_active ( state->id );
    }

    bool down = false;
    if ( xi_ui_state->active_id == state->id ) {
        down = true;
        if ( !xi_ui_state->update.mouse_down ) {
            xi_ui_release_active ( state->id );
        }

        if ( state->mode == xi_transform_mode_translation_m ) {
            const rv_view_info_t* view = &xi_ui_state->update.view_info;
            sm_vec_3f_t n = sm_quat_to_vec ( sm_quat ( view->transform.orientation ) );
            float d = sm_vec_3f_dot ( sm_vec_3f_neg ( n ), p );
            sm_vec_4f_t plane = { n.x, n.y, n.z, d };
            float depth;
            xi_ui_ray_plane_intersect ( &p, &depth, sm_vec_3f ( ray.origin ), sm_vec_3f ( ray.direction ), plane );
            state->position[0] = p.x;
            state->position[1] = p.y;
            state->position[2] = p.z;
        } else if ( state->mode == xi_transform_mode_rotation_m ) {
            float drag_scale = -1.f / 400;
            int64_t delta_x = xi_ui_state->update.mouse_delta_x;
            int64_t delta_y = xi_ui_state->update.mouse_delta_y;
            sm_quat_t orientation = sm_quat ( state->rotation );

            if ( delta_x != 0 ) {
                sm_vec_3f_t up = { 0, 1, 0 };
                //sm_vec_3f_t up = sm_quat_transform_f3 ( sm_quat ( state->rotation ), sm_vec_3f_set ( 0, 1, 0) );
                sm_quat_t q = sm_quat_axis_rotation ( up, delta_x * drag_scale );
                orientation = sm_quat_mul ( q, orientation );
            }

            if ( delta_y != 0 ) {
                //sm_vec_3f_t up = { 0, 1, 0 };
                //sm_vec_3f_t axis = sm_vec_3f_cross ( up, sm_vec_3f ( state->position ) );
                //axis = sm_vec_3f_norm ( axis );
                sm_vec_3f_t right = { 1, 0, 0 };
                sm_quat_t q = sm_quat_axis_rotation ( right, delta_y * drag_scale );
                orientation = sm_quat_mul ( q, orientation );
            }

            state->rotation[0] = orientation.x;
            state->rotation[1] = orientation.y;
            state->rotation[2] = orientation.z;
            state->rotation[3] = orientation.w;
        } else {
            std_not_implemented_m();
        }
    } else {
        down = false;
    }

    xi_draw_mesh_t draw = xi_draw_mesh_m (
        .pos_buffer = xform->pos_buffer,
        .nor_buffer = xform->nor_buffer,
        .uv_buffer = xform->uv_buffer,
        .idx_buffer = xform->idx_buffer,
        .idx_count = idx_count,
        .sort_order = state->sort_order,
        .traslation = { state->position[0], state->position[1], state->position[2] },
        .rotation = { state->rotation[0], state->rotation[1], state->rotation[2], state->rotation[3] },
        .scale = scale,
    );

    if ( down ) {
        draw.color[0] = 1;
        draw.color[1] = 0;
        draw.color[2] = 0;
        draw.color[3] = 1;
    } else if ( hovered ) {
        draw.color[0] = 1;
        draw.color[1] = 0.5;
        draw.color[2] = 0;
        draw.color[3] = 1;
    } else {
        draw.color[0] = 1;
        draw.color[1] = 1;
        draw.color[2] = 0;
        draw.color[3] = 1;
    }

    xi_workload_cmd_draw_mesh ( workload_handle, &draw );

    return down;
}

#if defined ( std_platform_win32_m )
#include <Commdlg.h>
#endif

bool xi_ui_file_pick ( std_buffer_t path_buffer, const char* initial_dir ) {
    if ( !initial_dir ) {
        initial_dir = ".";
    }
#if defined(std_platform_win32_m)
    std_log_info_m ( initial_dir );
    char* filename = path_buffer.base;
    OPENFILENAMEA ofn;
    ZeroMemory ( &ofn, sizeof ( ofn ) );
    filename[0] = '\0';
    ofn.lStructSize = sizeof ( OPENFILENAME );
    ofn.hwndOwner = ( HWND ) xi_ui_state->update.os_window_handle.win32.hwnd;
    ofn.lpstrFilter = NULL;
    ofn.lpstrCustomFilter = NULL;
    ofn.nMaxCustFilter = 0;
    ofn.nFilterIndex = 0;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = path_buffer.size;
    ofn.lpstrInitialDir = initial_dir;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrTitle = "";
    ofn.lpstrDefExt = NULL;
    ofn.Flags = OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST;
    
    int result = GetOpenFileName ( &ofn );
    return result != 0;
#else
    std_not_implemented_m();
    return false;
#endif
}
