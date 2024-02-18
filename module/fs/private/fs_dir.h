#pragma once

#include <fs.h>

bool    fs_dir_create ( const char* path );
bool    fs_dir_delete ( const char* path );
bool    fs_dir_copy ( const char* path, const char* dest, fs_already_existing_e overwrite );
bool    fs_dir_move ( const char* path, const char* dest, fs_already_existing_e overwrite );
size_t  fs_dir_iterate ( const char* path, fs_iterator_callback_f callback, void* arg );
size_t  fs_dir_get_files ( char** files, size_t files_cap, size_t file_cap, const char* path );
size_t  fs_dir_get_subdirs ( char** subdirs, size_t subdirs_cap, size_t subdir_cap, const char* path );
bool    fs_dir_get_info ( fs_dir_info_t* info, const char* path );
