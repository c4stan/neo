#pragma once

#include <xi.h>

#include <std_time.h>

typedef struct {
    // layer origin coords in *global* space (relative to the base OS window viewport), not relative to parent layer
    int32_t x;
    int32_t y;

    // layer size
    uint32_t width;
    uint32_t height;

    // *current* offset into the layer. Usually updated by add_element
    uint32_t line_offset;
    uint32_t line_offset_rev;
    uint32_t line_y;

    // stores the height of the *current* line. Used and cleared by newline(), updated by add_element()
    uint32_t line_height;

    // internal line padding to apply to each line of the layer
    uint32_t line_padding_x;
    uint32_t line_padding_y;

    // copied from the element that creates the layer, gets inherited by the children unless they override it with their own style
    xi_style_t style;
} xi_ui_layer_t;

typedef struct {
    // TODO move most of this state into a new xi_external_state_t struct
    wm_input_state_t input_state;
    int64_t mouse_delta_x;
    int64_t mouse_delta_y;
    std_tick_t mouse_down_tick;
    bool mouse_down;
    //bool mouse_double_down;
    std_tick_t current_tick;

    uint32_t os_window_width;
    uint32_t os_window_height;
} xi_ui_update_state_t;

typedef struct {
    xi_ui_update_state_t update;

    // mouse is over this element
    uint64_t hovered_id;
    // mouse has clicked/is currently dragging/interacting with this element
    uint64_t active_id;
    // used in case an element has multiple sub-elements that it wants to differentiate using custom sub-ids
    uint32_t hovered_sub_id;
    uint32_t active_sub_id;

    // used only by the get_active_id API
    uint64_t active_layer;

    bool cleared_hovered;
    bool cleared_active;

    xi_ui_layer_t layers[xi_ui_max_layers_m];
    uint32_t layer_count;

    // state of the base os window
    //int64_t base_x;
    //int64_t base_y;
    //int64_t base_width;
    //int64_t base_height;

    // TODO support stack of sections, not just one
    bool in_section;
    bool minimized_section;
} xi_ui_state_t;

void xi_ui_load ( xi_ui_state_t* state );
void xi_ui_reload ( xi_ui_state_t* state );
void xi_ui_unload ( void );

void xi_ui_update ( const wm_window_info_t* window_info, const wm_input_state_t* input_state );
void xi_ui_update_end ( void );

void xi_ui_window_begin ( xi_workload_h workload, xi_window_state_t* state );
void xi_ui_window_end ( xi_workload_h workload );

void xi_ui_section_begin ( xi_workload_h workload, xi_section_state_t* state );
void xi_ui_section_end ( xi_workload_h workload );

void xi_ui_label ( xi_workload_h workload, xi_label_state_t* state );
void xi_ui_switch ( xi_workload_h workload, xi_switch_state_t* state );
void xi_ui_slider ( xi_workload_h workload, xi_slider_state_t* state );
void xi_ui_button ( xi_workload_h workload, xi_button_state_t* state );
void xi_ui_select ( xi_workload_h workload, xi_select_state_t* state );
//void xi_ui_text ( xi_workload_h workload, xi_text_state_t* state, const xi_style_t* style );

void xi_ui_newline ( void );

uint64_t xi_ui_get_active_id ( void );

const xi_ui_update_state_t* xi_ui_get_update_state ( void );
