#pragma once

#include <fs.h>

#include <std_buffer.h>
#include <std_allocator.h>

/* ----------------------------------------------------------------------------
    All functions except fs_path_normalize and fs_path_normalize_inplace
    require the input path to be normalized. If not sure, always normalize
    before passing a path to the api.
---------------------------------------------------------------------------- */

size_t          fs_path_append              ( char* path, size_t path_cap, const char* append );
size_t          fs_path_append_n            ( char* path, size_t path_cap, const char* append, size_t n );
size_t          fs_path_pop                 ( char* path );
size_t          fs_path_normalize           ( char* dest, size_t cap, const char* path );
bool            fs_path_is_drive            ( const char* path );
size_t          fs_path_get_name            ( char* name, size_t cap, const char* path );
const char*     fs_path_get_name_ptr        ( const char* path );
bool            fs_path_get_info            ( fs_path_info_t* info, const char* path );
size_t          fs_path_get_absolute        ( char* dest, size_t cap, const char* path );

// Iterates tokens in a path. Returns NULL if there are no more.
// e.g. for 'C:/a/b.c' setting cursor on the string start gives the first token 'C:/' and consecutive calls return 'a/', 'b.c' and NULL.
// Token lengths are to be intended as displayed above, meaning, the eventual ending '/' is included.
const char*     fs_path_next_token          ( const char* path, const char* cursor );
const char*     fs_path_prev_token          ( const char* path, const char* cursor );
size_t          fs_path_token_len           ( const char* path, const char* cursor );
bool            fs_path_token_is_folder     ( const char* path, const char* cursor );
