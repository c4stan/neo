#pragma once

#include <std_module.h>

#define se_module_name_m se
std_module_export_m void* se_load ( void* );
std_module_export_m void* se_reload ( void*, void* );
std_module_export_m void se_unload ( void );

typedef uint64_t se_entity_h;
typedef uint64_t se_query_h;
typedef uint64_t se_entity_group_h;

typedef uint32_t se_component_e;
typedef uint64_t se_component_h;

#define se_null_handle_m UINT64_MAX
#define se_null_component_id_m UINT32_MAX
#define se_null_stream_id_m UINT8_MAX

typedef struct {
    se_component_e id;
    uint32_t stream_count;
    uint32_t streams[se_component_max_streams_m]; // stride of each stream
} se_component_layout_t;

#define se_component_layout_m( ... ) ( se_component_layout_t ) { \
    .id = 0, \
    .stream_count = 1, \
    .streams = { [0 ... se_component_max_streams_m - 1] = 0 }, \
    __VA_ARGS__ \
}

typedef struct {
    uint32_t component_count;
    se_component_layout_t components[se_max_components_per_entity_m];
} se_entity_family_params_t;

#define se_entity_family_params_m( ... ) ( se_entity_family_params_t ) { \
    .component_count = 0, \
    .components = { [0 ... se_max_components_per_entity_m - 1] = se_component_layout_m() }, \
    __VA_ARGS__ \
}

#define se_component_mask_block_count_m std_div_round_up_m ( se_max_component_types_m, sizeof ( uint64_t ) )
typedef struct {
    uint64_t u64[se_component_mask_block_count_m];
} se_component_mask_t;

#if 0
// TODO allow for inlining data instead of providing a pointer to it
typedef struct {
    uint8_t has_inline_data;
    uint8_t id;
    union {
        void* data;
        uint64_t inline_data[0]; // Should be inline_data[] but current clang doesn't like that
    };
} se_component_stream_update_t;

#define se_component_stream_update_m( ... ) ( se_component_stream_update_t ) { \
    .id = 0, \
    .data = NULL, \
    __VA_ARGS__ \
}

typedef struct {
    se_component_e id;
    uint32_t stream_count;
    se_component_stream_update_t streams[];
} se_component_update_t;

#define se_component_update_m( ... ) ( se_component_update_t ) { \
    .id = 0, \
    .stream_count = 0, \
    .streams = { [0 ... se_component_max_streams_m - 1] = se_component_stream_update_m() }, \
    __VA_ARGS__ \
}

#define se_component_update_monostream_m( _id, _data ) ( se_component_update_t ) { \
    .id = _id, \
    .stream_count = 1, \
    .streams = { se_component_stream_update_m ( .id = 0, .data = _data ) }, \
}

/*
    entity handle
    component count
    component[]
        id
        stream count
        streams[]
            id
            data

*/
typedef struct se_entity_update_t {
    se_entity_h entity;
    se_component_mask_t mask;
    uint32_t component_count;
    struct se_entity_update_t* next;
    se_component_update_t components[];
} se_entity_update_t;

#define se_entity_update_m( ... ) ( se_entity_update_t ) { \
    .component_count = 0, \
    .components = { [0 ... se_max_components_per_entity_m - 1] = se_component_update_m() }, \
    __VA_ARGS__ \
}

typedef struct {
    uint64_t entity_count;
    se_entity_update_t entities[];
} se_entity_params_t;

#define se_entity_params_m( ... ) ( se_entity_params_t ) { \
    .component_mask = { 0 }, \
    .update = se_entity_update_m(), \
    __VA_ARGS__ \
}
#else

typedef struct {
    uint8_t id;
    //std_buffer_t data;
    void* data;
} se_stream_update_t;

#define se_stream_update_m( ... ) ( se_stream_update_t ) { \
    .id = 0, \
    .data = NULL, \
    __VA_ARGS__ \
}

typedef struct {
    se_component_e id;
    uint32_t stream_count;
    se_stream_update_t streams[se_component_max_streams_m];
} se_component_update_t;

#define se_component_update_m( ... ) ( se_component_update_t ) { \
    .id = se_null_component_id_m, \
    .stream_count = 1, \
    .streams = {}, \
    __VA_ARGS__ \
}

typedef struct {
    uint32_t component_count;
    se_component_update_t components[se_max_components_per_entity_m];
} se_entity_update_t;

#define se_entity_update_m( ... ) ( se_entity_update_t ) { \
    .component_count = 1, \
    .components = {}, \
    __VA_ARGS__ \
}

typedef struct {
    char debug_name[se_debug_name_size_m];
    se_entity_update_t update;
} se_entity_params_t;

#define se_entity_params_m( ... ) ( se_entity_params_t ) { \
    .debug_name = {}, \
    .update = se_entity_update_m(), \
    __VA_ARGS__ \
}

#endif

typedef struct {
    uint32_t include_component_count;
    se_component_e include_components[se_max_components_per_entity_m];
    uint32_t exclude_component_count;
    se_component_e exclude_components[se_max_components_per_entity_m];
} se_query_params_t;

#define se_query_params_m( ... ) ( se_query_params_t ) { \
    .include_component_count = 0, \
    .include_components = { [0 ... se_max_components_per_entity_m - 1] = se_null_component_id_m }, \
    .exclude_component_count = 0, \
    .exclude_components = { [0 ... se_max_components_per_entity_m - 1] = se_null_component_id_m }, \
    __VA_ARGS__ \
}

// TODO dynamic allocate some of the following instead of using static arrays?
typedef struct {
    void* data;
    uint32_t count;
} se_data_stream_page_t;

typedef struct {
    uint32_t page_count;
    uint32_t page_capacity;
    uint32_t data_stride;
    se_data_stream_page_t pages[se_entity_family_max_pages_per_stream_m];
} se_data_stream_t;

typedef struct {
    uint32_t stream_count;
    se_data_stream_t streams[se_component_max_streams_m];
} se_component_data_t;

typedef struct {
    uint32_t entity_count;
    se_data_stream_t entities;  // se_entity_h
    se_component_data_t components[se_max_components_per_entity_m];
} se_query_result_t;

// TODO
#if 0
typedef struct {
    se_component_data_t* read_components[se_max_components_per_entity_m];
    se_component_data_t* write_components[se_max_components_per_entity_m];

    se_entity_h delete_entities[se_max_entities_m];
    uint64_t delete_count;
    // entity delete queue
    // entity create queue
} se_node_execute_args_t;

typedef void ( se_node_execute_f ) ( se_node_execute_args_t* node_args );

typedef struct {
    uint32_t read_components[se_max_components_per_entity_m];
    uint32_t read_components_count;
    uint32_t write_components[se_max_components_per_entity_m];
    uint32_t write_components_count;

    se_node_execute_f * execute_routine;

    char debug_name[se_debug_name_size_m];
} se_node_params_t;
#endif

typedef enum {
    se_property_f32_m,
    se_property_u32_m,
    se_property_i32_m,
    se_property_u64_m,
    se_property_i64_m,
    se_property_2f32_m,
    se_property_3f32_m,
    se_property_4f32_m,
    se_property_string_m,
    se_property_bool_m,
} se_property_e;

typedef struct {
    uint8_t stream;
    uint16_t offset;
    se_property_e type;
    char name[se_debug_name_size_m];
} se_property_t;

#define se_field_property_m( _stream, datatype, field, property ) ( se_property_t ) { \
    .stream = _stream, \
    .offset = std_field_offset_m ( datatype, field ), \
    .type = property, \
    .name = std_pp_string_m ( field ), \
}

typedef struct {
    uint32_t count;
    se_property_t properties[se_component_max_properties_m];
} se_component_properties_params_t;

#define se_component_properties_params_m(...) ( se_component_properties_params_t ) { \
    .count = 0, \
    .properties = {0}, \
    __VA_ARGS__ \
}

typedef struct {
    /*const*/ uint64_t entity;
    // TODO limit these to uint16_t and make the handle a u64?
    /*const*/ uint32_t component;
    /*const*/ uint32_t property;
} se_property_h;

typedef struct {
    uint32_t id;
    uint32_t property_count;
    char name[se_debug_name_size_m];
    se_property_t properties[se_component_max_properties_m];
    se_property_h handles[se_component_max_properties_m];
} se_component_properties_t;

typedef struct {
    uint32_t component_count;
    char name[se_debug_name_size_m];
    se_component_properties_t components[se_max_components_per_entity_m];
} se_entity_properties_t;

typedef struct {
    void ( *create_entity_family ) ( const se_entity_family_params_t* params );
    void ( *destroy_entity_family ) ( se_component_mask_t mask );

    se_entity_h ( *create_entity ) ( const se_entity_params_t* params );
    void ( *destroy_entity ) ( se_entity_h entity );

    void ( *query_entities ) ( se_query_result_t* result, const se_query_params_t* params );

    const char* ( *get_entity_name ) ( se_entity_h entity );
    void* ( *get_entity_component ) ( se_entity_h entity, se_component_e component, uint8_t stream );

    void ( *set_entity_name ) ( se_entity_h entity, const char* name );
    void ( *set_component_properties ) ( se_component_e component, const char* name, const se_component_properties_params_t* params );
    size_t ( *get_entity_list ) ( se_entity_h* out_entities, size_t cap );
    void ( *get_entity_properties ) ( se_entity_properties_t* out_props, se_entity_h entity_handle );
} se_i;

#include <se.inl>
