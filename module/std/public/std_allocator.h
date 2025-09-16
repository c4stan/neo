#pragma once

#include <std_byte.h>
#include <std_compiler.h>

/*
    Virtual memory page allocator
*/
size_t std_virtual_page_size ( void );
size_t std_virtual_page_align ( size_t size );

void* std_virtual_reserve ( size_t size );
bool std_virtual_map ( void* begin, void* end ); // begin included, end excluded
bool std_virtual_unmap ( void* begin, void* end );
bool std_virtual_free ( void* begin, void* end ); // on Win32 this range must match the entire reserved range

/*
    Virtual heap allocator
    TODO rename to global_heap? heap? ???
*/
#if std_build_debug_m
typedef struct {
    char module[32];
    char file[32];
    char function[32];
    size_t line;
} std_alloc_scope_t;

//#define std_alloc_scope_m() ( std_alloc_scope_t ) { .module = std_pp_eval_string_m(std_module_name_m), .file = std_pp_eval_string_m(std_file_name_m), .function = std_func_name_m, .line = std_line_num_m }
#define std_alloc_scope_m() ( { \
    std_alloc_scope_t s; \
    std_str_copy_static_m ( s.module, std_pp_eval_string_m ( std_module_name_m ) ); \
    std_str_copy_static_m ( s.file, std_pp_eval_string_m ( std_file_name_m ) ); \
    std_str_copy_static_m ( s.function, std_func_name_m ); \
    s.line = std_line_num_m; \
    s; \
 } )

void*   std_virtual_heap_alloc ( size_t size, size_t align, std_alloc_scope_t scope );
#define std_virtual_heap_alloc_m( size, align ) ( std_virtual_heap_alloc ( size, align, std_alloc_scope_m() ) )
#define std_virtual_heap_alloc_array_m( type, count ) ( type* ) ( std_virtual_heap_alloc_m ( sizeof ( type ) * (count), std_alignof_m ( type ) ) )
#define std_virtual_heap_alloc_struct_m( type ) std_virtual_heap_alloc_array_m ( type, 1 )
#else
void*   std_virtual_heap_alloc ( size_t size, size_t align );
#define std_virtual_heap_alloc_m( size, align ) ( std_virtual_heap_alloc ( size, align ) )
#define std_virtual_heap_alloc_array_m( type, count ) ( type* ) ( std_virtual_heap_alloc_m ( sizeof ( type ) * (count), std_alignof_m ( type ) ) )
#define std_virtual_heap_alloc_struct_m( type ) std_virtual_heap_alloc_array_m ( type, 1 )
#endif
bool    std_virtual_heap_free ( void* ptr );

typedef struct {
    // TODO these are not accurate, need to find a way to query OS for these numbers
    uint64_t reserved_size;
    uint64_t mapped_size;
    uint64_t used_heap_size;
    uint64_t total_heap_size;
} std_allocator_info_t;

void std_allocator_info ( std_allocator_info_t* info );

typedef struct {
    uint64_t allocated_size;
    char name[std_module_name_max_len_m];
} std_allocator_module_info_record_t;

typedef struct {
    std_allocator_module_info_record_t* modules;
    uint32_t count;
} std_allocator_module_info_t;

void std_virtual_heap_allocator_module_info ( std_allocator_module_info_t* info );

// Just a utility buffer struct. Can be used as return value, or to store a memory segment without having to split it into 2 separate fields
typedef struct {
    void* base;
    size_t size;
} std_buffer_t;

#define std_buffer_m( ... ) ( std_buffer_t ) { \
    __VA_ARGS__ \
}
#define std_buffer_struct_m( item ) std_buffer_m ( .base = item, .size = sizeof ( *item ) )
#define std_buffer_static_array_m( array ) std_buffer_m ( .base = array, .size = sizeof ( array ) )

// TODO pass len as param to str functions instead of computing it inside
// Linear allocator based on virtual memory. The mapped segment will grow until the whole reserved range is full. It will not grow further.
typedef struct {
    void* begin;
    void* top;
    void* end;
    void* virtual_end;
} std_stack_t;

std_stack_t std_stack ( void* base, size_t mapped_size, size_t virtual_size );
std_stack_t std_stack_create ( size_t virtual_size );
void        std_stack_destroy ( std_stack_t* stack );
void*       std_stack_alloc ( std_stack_t* stack, size_t size );
void*       std_stack_write ( std_stack_t* stack, const void* data, size_t size );
void*       std_stack_align ( std_stack_t* stack, size_t align );
void*       std_stack_align_zero ( std_stack_t* stack, size_t align );
void*       std_stack_alloc_align ( std_stack_t* stack, size_t size, size_t align );
void*       std_stack_write_align ( std_stack_t* stack, const void* data, size_t size, size_t align );
void        std_stack_clear ( std_stack_t* stack );
void        std_stack_free ( std_stack_t* stack, size_t size );
uint64_t    std_stack_used_size ( const std_stack_t* stack );
uint64_t    std_stack_used_size_from ( const std_stack_t* stack, void* base );
uint64_t    std_stack_unused_size ( const std_stack_t* stack );

char*       std_stack_string_copy ( std_stack_t* stack, const char* str );
char*       std_stack_string_copy_format ( std_stack_t* stack, const char* str, ... );
char*       std_stack_string_append ( std_stack_t* stack, const char* str );
char*       std_stack_string_append_format ( std_stack_t* stack, const char* str, ... );
char*       std_stack_string_append_char ( std_stack_t* stack, char c );
void        std_stack_string_pop ( std_stack_t* stack );

#define std_fixed_stack_m( base, size ) std_stack ( base, size, size )
#define std_static_stack_m( array ) std_fixed_stack_m ( array, sizeof ( array ) )
#define std_stack_alloc_array_m( stack, type, count ) ( type* ) std_stack_alloc_align ( stack, sizeof ( type ) * (count), std_alignof_m ( type ) )
#define std_stack_alloc_m( stack, type ) std_stack_alloc_array_m ( stack, type, 1 )
#define std_stack_write_m( stack, data ) std_stack_write ( stack, data, sizeof ( *data ) )
#define std_stack_write_array_m( stack, base, count ) std_stack_write ( stack, base, sizeof ( *base ) * count )
#define std_stack_array_count_m( stack, type ) ( ( (stack)->top - (stack)->begin ) / sizeof ( type ) )

/*
    Tagged allocator
    // TODO remove?
*/
#if 0
size_t       std_tagged_page_size ( void );
std_buffer_t std_tagged_alloc ( size_t size, uint64_t tag );
void         std_tagged_free ( uint64_t tag );
#endif
