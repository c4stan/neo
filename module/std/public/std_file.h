#pragma once

#include <std_platform.h>
#include <std_time.h>

typedef uint64_t std_file_h;
#define std_file_null_handle_m UINT64_MAX

typedef enum {
    std_path_is_file_m      = 1 << 0,
    std_path_is_directory_m = 1 << 1,
    std_path_non_existent_m = 1 << 2,
} std_path_flags_t;

typedef struct {
    std_path_flags_t flags;
    std_timestamp_t creation_time;
} std_path_info_t;

typedef enum {
    std_file_compressed_m  = 1 << 0,
    std_file_encrypted_m   = 1 << 1,
    std_file_hidden_m      = 1 << 2,
    std_file_readonly_m    = 1 << 3,
    std_file_os_use_m      = 1 << 4,
} std_file_flags_t;

typedef enum {
    std_file_read_m         = 1 << 0,
    std_file_write_m        = 1 << 1,
    std_file_append_m       = 1 << 1,
} std_file_access_t;

typedef enum {
    std_file_start_m,
    std_file_end_m,
    std_file_current_m,
} std_file_point_t;

typedef enum {
    std_file_map_read_m             = 1 << 0,
    std_file_map_write_m            = 1 << 1,
    std_file_map_execute_m          = 1 << 2,
    std_file_map_copy_on_write_m    = 1 << 3,
} std_file_map_permits_t;

typedef struct {
    std_timestamp_t creation_time;
    std_timestamp_t last_access_time;
    std_timestamp_t last_write_time;
    uint64_t size;
    uint64_t file_id;
    uint32_t volume_id;
    std_file_flags_t flags;
} std_file_info_t;

typedef enum {
    std_directory_compressed_m  = 1 << 0,
    std_directory_encrypted_m   = 1 << 1,
    std_directory_hidden_m      = 1 << 2,
    std_directory_os_use_m      = 1 << 3,
} std_directory_flags_t;

typedef struct {
    std_timestamp_t creation_time;
    std_timestamp_t last_access_time;
    std_timestamp_t last_write_time;
    std_directory_flags_t flags;
} std_directory_info_t;

typedef enum {
    std_path_already_existing_overwrite_m,
    std_path_already_existing_fail_m,       // TODO
    std_path_already_existing_alias_m       // TODO use a similar but different destination name? Do we ever want this? Can the OS pick one for me?
} std_path_already_existing_e;

typedef void ( std_directory_iterator_callback_f ) ( const char* name, std_path_flags_t flags, void* arg );

#define std_file_read_error_m UINT64_MAX

size_t      std_path_append     ( char* path, size_t cap, const char* append );
size_t      std_path_pop        ( char* path );
size_t      std_path_normalize  ( char* dest, size_t cap, const char* path );
bool        std_path_is_drive   ( const char* path );
size_t      std_path_name       ( char* name, size_t cap, const char* path );
const char* std_path_name_ptr   ( const char* path );
bool        std_path_info       ( std_path_info_t* info, const char* path );
size_t      std_path_absolute   ( char* dest, size_t dest_cap, const char* path );

bool        std_directory_create ( const char* path );
bool        std_directory_destroy ( const char* path );
bool        std_directory_copy ( const char* path, const char* dest, std_path_already_existing_e already_existing );
bool        std_directory_move ( const char* path, const char* dest, std_path_already_existing_e already_existing );
size_t      std_directory_iterate ( const char* path, std_directory_iterator_callback_f cb, void* arg );
size_t      std_directory_files ( char** files, size_t files_cap, size_t file_cap, const char* path );
size_t      std_directory_subdirs ( char** subdirs, size_t subdirs_cap, size_t subdir_cap, const char* path );
bool        std_directory_info ( std_directory_info_t* info, const char* path );

std_file_h  std_file_create ( const char* path, std_file_access_t access, std_path_already_existing_e already_existing );
bool        std_file_destroy ( std_file_h file );
bool        std_file_path_destroy ( const char* path );
bool        std_file_copy ( std_file_h file, const char* dest, std_path_already_existing_e already_existing );
bool        std_file_path_copy ( const char* path, const char* dest, std_path_already_existing_e already_existing );
bool        std_file_move ( std_file_h file, const char* dest, std_path_already_existing_e already_existing );
bool        std_file_path_move ( const char* path, const char* dest, std_path_already_existing_e already_existing );
std_file_h  std_file_open ( const char* path, std_file_access_t access );
bool        std_file_close ( std_file_h file );
void*       std_file_map ( std_file_h file, size_t size, std_file_map_permits_t permits );
bool        std_file_unmap ( std_file_h file, void* mapping );
uint64_t    std_file_read ( void* dest, size_t read_size, std_file_h file );
uint64_t    std_file_path_read ( void* dest, uint64_t cap, const char* path );
bool        std_file_write ( std_file_h file, const void* source, size_t write_size );
bool        std_file_seek ( std_file_h file, std_file_point_t base, int64_t offset );
bool        std_file_info ( std_file_info_t* info, std_file_h file );
bool        std_file_path_info ( std_file_info_t* info, const char* path );
size_t      std_file_path ( char* path, size_t cap, std_file_h file );
