#pragma once

#include <fs.h>

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

std_buffer_t    fs_file_map ( fs_file_h file, fs_map_permits_t permits );
bool            fs_file_unmap ( std_buffer_t buffer );

bool        fs_file_read ( size_t* actual_read_size, std_buffer_t dest, fs_file_h file );
bool        fs_file_write ( size_t* actual_write_size, std_buffer_t source, fs_file_h file );

bool        fs_file_seek ( fs_file_h file, fs_file_point_t base, int64_t offset );

bool        fs_file_get_info ( fs_file_info_t* info, fs_file_h file );
bool        fs_file_path_get_info ( fs_file_info_t* info, const char* path );
size_t      fs_file_get_path ( char* path, size_t cap, fs_file_h file );
