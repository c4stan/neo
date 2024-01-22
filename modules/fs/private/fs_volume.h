#pragma once

#include <fs.h>

/*
Mounted folders are not supported. If the system has a mounted folder for a volume, it is possible that get_path returns that folder instead of the volume root path.
*/

fs_list_h   fs_volume_get_first ( fs_volume_h* volume );
bool        fs_volume_get_next ( fs_list_h* list, fs_volume_h* volume );
void        fs_volume_close_list ( fs_list_h list );

size_t      fs_volume_get_count ( void );
size_t      fs_volume_get ( fs_volume_h* volumes, size_t cap );

size_t      fs_volume_get_path ( char* path, size_t cap, const fs_volume_h* volume );
bool        fs_volume_get_info ( fs_volume_info_t* info, const fs_volume_h* volume );
