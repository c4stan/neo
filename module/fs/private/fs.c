#include <fs.h>

#include "fs_dir.h"
#include "fs_file.h"
#include "fs_path.h"
#include "fs_volume.h"
#include "fs_state.h"

static void fs_api_init ( fs_i* fs ) {
    // drive
    fs->get_first_volume = fs_volume_get_first;
    fs->get_next_volume = fs_volume_get_next;
    fs->close_volume_list = fs_volume_close_list;
    fs->get_volumes_count = fs_volume_get_count;
    fs->get_volumes = fs_volume_get;
    fs->get_volume_info = fs_volume_get_info;
    fs->get_volume_path = fs_volume_get_path;
    // path
    fs->append_path = fs_path_append;
    fs->pop_path = fs_path_pop;
    fs->normalize_path = fs_path_normalize;
    fs->is_drive = fs_path_is_drive;
    fs->get_path_name = fs_path_get_name;
    fs->get_path_name_ptr = fs_path_get_name_ptr;
    fs->get_path_info = fs_path_get_info;
    fs->get_absolute_path = fs_path_get_absolute;
    // dir
    fs->create_dir = fs_dir_create;
    fs->delete_dir = fs_dir_delete;
    fs->copy_dir = fs_dir_copy;
    fs->move_dir = fs_dir_move;
    fs->iterate_dir = fs_dir_iterate;
    fs->get_dir_files = fs_dir_get_files;
    fs->get_dir_subdirs = fs_dir_get_subdirs;
    fs->get_dir_info = fs_dir_get_info;
    // file
    fs->create_file = fs_file_create;
    fs->create_file_path = fs_file_path_create;
    fs->delete_file = fs_file_delete;
    fs->delete_file_path = fs_file_path_delete;
    fs->copy_file = fs_file_copy;
    fs->copy_file_path = fs_file_path_copy;
    fs->move_file = fs_file_move;
    fs->move_file_path = fs_file_path_move;
    fs->open_file = fs_file_open;
    fs->close_file = fs_file_close;
    fs->map_file = fs_file_map;
    fs->unmap_file = fs_file_unmap;
    fs->read_file = fs_file_read;
    fs->write_file = fs_file_write;
    fs->seek_file = fs_file_seek;
    fs->get_file_info = fs_file_get_info;
    fs->get_file_path_info = fs_file_path_get_info;
    fs->get_file_path = fs_file_get_path;
}

void* fs_load ( void* std_runtime ) {
    std_runtime_bind ( std_runtime );

    fs_state_t* state = fs_state_alloc();

    fs_api_init ( &state->api );

    return &state->api;
}

void fs_reload ( void* std_runtime, void* api ) {
    std_runtime_bind ( std_runtime );

    std_auto_m state = ( fs_state_t* ) api;

    fs_api_init ( &state->api );
}

void fs_unload ( void ) {
    std_noop_m;
}
