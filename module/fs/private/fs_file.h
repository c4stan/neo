#pragma once

#include <fs.h>

typedef struct {
    fs_file_h file_handle;
    std_virtual_stack_t stack;
} fs_virtual_file_t;

typedef struct {

} fs_file_state_t;

fs_file_h   fs_file_create ( const char* path, fs_file_access_t access, fs_already_existing_e already_existing );
bool        fs_file_path_create ( const char* path, fs_file_access_t access, fs_already_existing_e already_existing ); // TODO wtf is this?

bool        fs_file_delete ( fs_file_h file );
bool        fs_file_path_delete ( const char* path );

bool        fs_file_copy ( fs_file_h file, const char* dest, fs_already_existing_e already_existing );
bool        fs_file_path_copy ( const char* path, const char* dest, fs_already_existing_e already_existing );

bool        fs_file_move ( fs_file_h file, const char* dest, fs_already_existing_e already_existing );
bool        fs_file_path_move ( const char* path, const char* dest, fs_already_existing_e already_existing );

fs_file_h   fs_file_open ( const char* path, fs_file_access_t access );
bool        fs_file_close ( fs_file_h file );

void*       fs_file_map ( fs_file_h file, size_t size, fs_map_permits_t permits );
bool        fs_file_unmap ( fs_file_h file, void* base );

uint64_t    fs_file_read ( void* dest, size_t capacity, fs_file_h file );
bool        fs_file_write ( fs_file_h file, const void* source, size_t size );

bool        fs_file_seek ( fs_file_h file, fs_file_point_t base, int64_t offset );

bool        fs_file_get_info ( fs_file_info_t* info, fs_file_h file );
bool        fs_file_path_get_info ( fs_file_info_t* info, const char* path );
size_t      fs_file_get_path ( char* path, size_t cap, fs_file_h file );

std_buffer_t fs_file_path_read_alloc ( const char* path );
