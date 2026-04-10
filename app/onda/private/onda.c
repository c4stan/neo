#include <onda.h>

#include <std_app.h>
#include <std_log.h>
#include <std_process.h>
#include <std_allocator.h>
#include <std_file.h>
#include <std_string.h>
#include <std_list.h>

#include <net.h>
#include <aud.h>
#include <wm.h>

#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"
#include "foxen-flac.h"

#define onda_subpath_size_m 512
#define onda_stream_chunk_size_m (1024ull * 64 * 1)

#define onda_rootpath_size_m 128
#define onda_path_size_m ( onda_rootpath_size_m + onda_subpath_size_m )

typedef struct {
    net_socket_h socket;
    net_socket_address_t address;
    char ip[32];
} onda_server_connection_t;

typedef struct {
    std_string_t root_path;
    char root_path_buffer[onda_path_size_m];
    onda_server_connection_t* connections_array;
    onda_server_connection_t* connections_freelist;
    uint64_t* connections_bitset;
} onda_server_state_t;

typedef struct {
    char** subdirs;
    char** files;
    uint32_t subdir_count;
    uint32_t file_count;
} list_result_t;

typedef struct {
    aud_i* aud;
    wm_i* wm;
    aud_device_h device;
    wm_window_h window;
    list_result_t list;
    std_stack_t list_stack;
    float volume;
    std_string_t path;
    char path_buffer[onda_path_size_m];
} onda_client_state_t;

typedef struct {
    std_app_i api;
    bool boot;
    bool is_server;
    net_i* net;
    net_socket_h socket;
    std_stack_t stack;
    union {
        onda_server_state_t server;
        onda_client_state_t client;
    };
} onda_state_t;

#define onda_state_m( ... ) ( onda_state_t ) { \
    __VA_ARGS__ \
}

std_module_declare_state_m ( onda );
std_module_implement_state_m ( onda );

static void onda_print_help ( void ) {
    char buffer[128];
    std_string_t string = std_static_string_m ( buffer );
    std_string_append ( &string, "onda ( -s | -c )\n" );
    std_string_append ( &string, std_fmt_tab_m "if -s: PORT PATH\n" );
    std_string_append ( &string, std_fmt_tab_m "if -c: IP PORT\n" );
    std_log_info_m ( buffer );
}

#define onda_server_max_connections_m 128

static bool onda_boot_server ( uint16_t port, const char* path ) {
    net_i* net = onda_state->net;
    onda_state->is_server = true;

    onda_state->server.connections_array = std_virtual_heap_alloc_array_m ( onda_server_connection_t, onda_server_max_connections_m );
    onda_state->server.connections_freelist = std_freelist_m ( onda_state->server.connections_array, onda_server_max_connections_m );
    onda_state->server.connections_bitset = std_virtual_heap_alloc_array_m ( uint64_t, std_bitset_u64_count_m ( onda_server_max_connections_m ) );
    std_mem_zero_array_m ( onda_state->server.connections_bitset, std_bitset_u64_count_m ( onda_server_max_connections_m ) );

    onda_state->server.root_path = std_static_string_m ( onda_state->server.root_path_buffer );
    std_path_normalize ( &onda_state->server.root_path, path );
    std_path_info_t path_info;
    bool success = std_path_info ( &path_info, onda_state->server.root_path.str );
    if ( !success ) {
        std_log_error_m ( "Invalid path" );
        return false;
    }
    if ( path_info.flags != std_path_is_directory_m ) {
        std_log_error_m ( "Path must be an existing directory" );
        return false;
    }

    net_socket_address_t address = net_socket_address_m (
        .port = port,
    );
    net->ip_string_to_bytes ( &address.ip, "0.0.0.0", net_address_family_ip4_m );

    onda_state->socket = net->create_socket ( &net_socket_params_m (
        .is_blocking = false,
    ) );
    success = net->bind_socket ( onda_state->socket, &address );
    if ( !success ) {
        std_log_error_m ( "Failed to bind socket address" );
        return false;
    }

    success = net->listen_for_connections ( onda_state->socket );
    if ( !success ) {
        std_log_error_m ( "Failed to listen to server socket" );
        return false;
    }

    return true;
}

static void onda_server_add_connection ( net_socket_h socket, net_socket_address_t address ) {
    net_i* net = onda_state->net;
    onda_server_connection_t* connection = std_list_pop ( &onda_state->server.connections_freelist );
    std_assert_m ( connection );
    connection->socket = socket;
    connection->address = address;
    net->ip_bytes_to_string ( connection->ip, &address.ip, net_address_family_ip4_m );
    uint64_t idx = connection - onda_state->server.connections_array;
    std_bitset_set ( onda_state->server.connections_bitset, idx );
    std_log_info_m ( "Serving new client " std_fmt_str_m ":" std_fmt_u16_m, connection->ip, address.port );
}

static void onda_server_remove_connection ( onda_server_connection_t* connection ) {
    std_list_push ( &onda_state->server.connections_freelist, connection );
    uint64_t idx = connection - onda_state->server.connections_array;
    std_bitset_clear ( onda_state->server.connections_bitset, idx );
}

// left alt + up/down : volume
// left alt + left/right : skip
// left alt + left ctrl + up : pause/resume
// left alt + left ctrl + down : stop
static void onda_client_print_help ( void ) {
    std_log_m ( std_log_level_custom_m, "list - List current path ids and filenames\n" );
    std_log_m ( std_log_level_custom_m, "open <id> - Open folder and update path\n" );
    std_log_m ( std_log_level_custom_m, "back - Close current folder and update path\n" );
    std_log_m ( std_log_level_custom_m, "exit - Exit application\n" );
    std_log_m ( std_log_level_custom_m, "play <id> - Play a media file or entire folder contents.\n" );
    std_log_m ( std_log_level_custom_m, std_fmt_tab_m "Volume: " std_fmt_tab_m "left alt + up/down\n" );
    std_log_m ( std_log_level_custom_m, std_fmt_tab_m "Skip: " std_fmt_tab_m std_fmt_tab_m "left alt + left/right\n" );
    std_log_m ( std_log_level_custom_m, std_fmt_tab_m "Pause/Resume: " std_fmt_tab_m "left alt + left ctrl + up\n" );
    std_log_m ( std_log_level_custom_m, std_fmt_tab_m "Stop: " std_fmt_tab_m std_fmt_tab_m "left alt + left ctrl + down\n" );
}

static bool onda_boot_client ( const char* server_ip, uint16_t server_port ) {
    net_i* net = onda_state->net;
    onda_state->is_server = false;

    onda_state->socket = net->create_socket ( &net_socket_params_m () );

    net_socket_address_t server_address = net_socket_address_m ( 
        .port = server_port
    );
    bool success = net->ip_string_to_bytes ( &server_address.ip, server_ip, net_address_family_ip4_m );
    if ( !success ) {
        std_log_error_m ( "Invalid server ip address" );
        return false;
    }

    success = net->connect_socket ( onda_state->socket, &server_address );
    if ( !success ) {
        std_log_error_m ( "Failed to connect to server" );
        return false;
    }

    aud_i* aud = std_module_load_m ( aud_module_name_m );
    onda_state->client.aud = aud;
    size_t device_count = aud->get_devices_count();
    std_log_info_m ( "Device count: " std_fmt_size_m, device_count );
    aud_device_h devices[aud_device_max_devices_m];
    aud->get_devices ( devices, aud_device_max_devices_m );
    for ( size_t i = 0; i < device_count; ++i ) {
        aud_device_info_t info;
        success = aud->get_device_info ( &info, devices[i] );
        std_assert_m ( success );
        std_log_info_m ( "Device " std_fmt_size_m ": " std_fmt_str_m, i, info.name );
    }

    aud_device_h device = devices[0];
    aud_device_params_t device_params = aud_device_params_m (
        .channel_count = 2,
        .sample_frequency = 48000,
        .bits_per_sample = 32
    );
    success = aud->activate_device ( device, &device_params );
    std_assert_m ( success );
    std_log_info_m ( "\tChannels: " std_fmt_size_m, device_params.channel_count );
    std_log_info_m ( "\tSample frequency: " std_fmt_size_m, device_params.sample_frequency );
    std_log_info_m ( "\tbpp: " std_fmt_size_m, device_params.bits_per_sample );
    aud_device_info_t device_info;
    aud->get_device_info ( &device_info, device );
    std_log_info_m ( "Picking device 0: " std_fmt_str_m, device_info.name );
    onda_state->client.device = device;

    wm_i* wm = std_module_load_m ( wm_module_name_m );
    onda_state->client.wm = wm;
    onda_state->client.window = wm->get_console_window();

    onda_state->client.path = std_static_string_m ( onda_state->client.path_buffer );
    onda_state->client.path_buffer[0] = '\0';
    std_mem_zero_m ( &onda_state->client.list );
    onda_state->client.volume = 0.2f;

    onda_state->client.list_stack = std_stack_create ( 1024ull * 1024 * 1024 );

    onda_client_print_help();

    return true;
}

static bool onda_boot ( void ) {
    onda_state->boot = true;

    std_process_info_t process_info;
    std_process_info ( &process_info, std_process_this() );

    if ( process_info.args_count < 3 ) {
        onda_print_help();
        std_log_error_m ( "Bad arguments" );
        return false;
    }

    onda_state->stack = std_stack_create ( 1024ull * 1024 * 1024 );

    const char* mode_arg = process_info.args[1];
    if ( std_str_cmp ( mode_arg, "-s" ) == 0 ) {
        const char* port_arg = process_info.args[2];
        uint16_t port = std_str_to_u16 ( port_arg );
        const char* path_arg = process_info.args[3];
        onda_boot_server ( port, path_arg );
        return true;
    } else if ( std_str_cmp ( mode_arg, "-c" ) == 0 ) {
        const char* ip_arg = process_info.args[2];
        const char* server_port_arg = process_info.args[3];
        uint16_t server_port = std_str_to_u16 ( server_port_arg );
        return onda_boot_client ( ip_arg, server_port );
    } else {
        std_log_error_m ( "Bad mode argument: " std_fmt_str_m, mode_arg );
        return false;
    }

    return true;
}

static list_result_t onda_build_list ( std_stack_t* stack, const char* client_path ) {
    // TODO dynamic subdir/file count
    uint32_t max_subdirs = 256;
    uint32_t max_files = 256;

    std_string_t path = std_stack_alloc_string ( stack, onda_path_size_m );
    std_string_copy ( &path, onda_state->server.root_path.str );
    std_path_append ( &path, client_path );

    char** subdirs = std_stack_alloc_array_m ( stack, char*, max_subdirs );
    for ( uint32_t i = 0; i < max_subdirs; ++i ) {
        subdirs[i] = std_stack_alloc ( stack, onda_subpath_size_m );
    }
    size_t subdir_count = std_directory_subdirs ( subdirs, max_subdirs, onda_subpath_size_m, path.str );
    char** raw_files = std_stack_alloc_array_m ( stack, char*, max_files );
    for ( uint32_t i = 0; i < max_files; ++i ) {
        raw_files[i] = std_stack_alloc ( stack, onda_subpath_size_m );
    }
    size_t raw_file_count = std_directory_files ( raw_files, max_files, onda_subpath_size_m, path.str );

    char** files = std_stack_alloc_array_m ( stack, char*, max_files );
    uint32_t file_count = 0;
    for ( uint32_t i = 0; i < raw_file_count; ++i ) {
        char* ext = std_path_ext ( &std_string_m ( .str = raw_files[i], .len = std_str_len ( raw_files[i] ), .cap = onda_subpath_size_m ) );
        if ( std_str_cmp ( ext, "mp3" ) != 0 && std_str_cmp ( ext, "flac" ) != 0 ) {
            continue;
        }

        files[file_count++] = raw_files[i];
    }

    list_result_t result = {
        .subdirs = subdirs,
        .files = files,
        .subdir_count = subdir_count,
        .file_count = file_count,
    };
    return result;
}

typedef struct {
    uint32_t total_size;
    uint32_t subdir_count;
    uint32_t file_count;
    char data[];
} onda_list_header_t;

static bool onda_server_send_list ( net_socket_h client_socket, std_stack_t* stack, const list_result_t* list ) {
    net_i* net = onda_state->net;

    std_auto_m header = std_stack_alloc_m ( stack, onda_list_header_t );
    header->subdir_count = list->subdir_count;
    header->file_count = list->file_count;

    // TODO dynamic size paths
    for ( uint32_t i = 0; i < list->subdir_count; ++i ) {
        char* subdir = std_stack_alloc ( stack, onda_subpath_size_m );
        std_str_copy ( subdir, onda_subpath_size_m, list->subdirs[i] );
    }

    for ( uint32_t i = 0; i < list->file_count; ++i ) {
        char* file = std_stack_alloc ( stack, onda_subpath_size_m );
        std_str_copy ( file, onda_subpath_size_m, list->files[i] );
    }

    size_t msg_size = std_stack_used_size_from ( stack, header );
    header->total_size = msg_size;

    size_t write_size = net->write_connected_socket ( client_socket, header, msg_size );
    return write_size > 0;
}

typedef enum {
    onda_client_stream_type_mp3_m,
    onda_client_stream_type_flac_m,
    onda_client_stream_type_unknown_m,
} onda_client_stream_type_e;

typedef struct {
    uint32_t label_size;
    uint32_t media_size;
    uint32_t sample_count;
    uint32_t sample_frequency;
    uint32_t channel_count;
    uint32_t bits_per_sample;
    onda_client_stream_type_e type;
    char data[];
} onda_media_header_t;

static bool onda_server_stream_media ( net_socket_h client_socket, std_stack_t* stack, const char* path ) {
    net_i* net = onda_state->net;
    std_file_h file = std_file_open ( path, std_file_read_m );
    
    std_file_info_t file_info;
    std_file_info ( &file_info, file );
    
    uint64_t path_len = std_str_len ( path );
    std_string_t path_string = std_fixed_string_len_m ( path, path_len );
    char* label = std_path_relative_to ( &path_string, onda_state->server.root_path.str );
    size_t label_size = std_str_len ( label ) + 1;
    
    void* begin = std_stack_alloc ( stack, sizeof ( onda_media_header_t ) );
    std_auto_m header = ( onda_media_header_t* ) begin;
    header->label_size = label_size;
    header->media_size = file_info.size;
    void* label_data = std_stack_alloc ( stack, label_size );
    std_str_copy ( label_data, label_size, label );
    void* media_data = std_stack_alloc ( stack, file_info.size );
    std_file_read ( media_data, file_info.size, file );
    std_file_close ( file );

    char* ext = std_path_ext ( &path_string );
    if ( ext ) {
        if ( std_str_cmp ( ext, "mp3" ) == 0 ) {
            header->type = onda_client_stream_type_mp3_m;

            mp3dec_ex_t dec;
            mp3dec_ex_open_buf ( &dec, media_data, file_info.size, MP3D_SEEK_TO_SAMPLE );
            header->sample_frequency = dec.info.hz;
            header->channel_count = dec.info.channels;
            header->bits_per_sample = 16;
            // compute total sample count
            uint64_t sample_count = 0;
            uint32_t decode_offset = 0;
            mp3dec_t mp3dec;
            mp3dec_init ( &mp3dec );
            while ( decode_offset < file_info.size ) {
                mp3dec_frame_info_t info;
                int samples = mp3dec_decode_frame ( &mp3dec, media_data + decode_offset, file_info.size - decode_offset, NULL, &info );
                if ( samples > 0 ) {
                    sample_count += ( uint64_t ) samples * info.channels;
                }
                decode_offset += info.frame_bytes;
            }
            header->sample_count = sample_count;
        } else if ( std_str_cmp ( ext, "flac" ) == 0 ) {
            header->type = onda_client_stream_type_flac_m;

            // process whole file, get info after
            fx_flac_t* flac = FX_FLAC_ALLOC_DEFAULT();
            uint32_t decode_offset = 0;
            uint32_t decode_size = file_info.size - decode_offset;
            while ( fx_flac_process ( flac, media_data + decode_offset, &decode_size, NULL, NULL) < FLAC_END_OF_METADATA ) {
                decode_offset += decode_size;
                decode_size = file_info.size - decode_offset;
            }

            header->sample_frequency = fx_flac_get_streaminfo ( flac, FLAC_KEY_SAMPLE_RATE );
            header->channel_count = fx_flac_get_streaminfo ( flac, FLAC_KEY_N_CHANNELS );
            header->bits_per_sample = 32;
            header->sample_count = fx_flac_get_streaminfo ( flac, FLAC_KEY_N_SAMPLES ) * header->channel_count;
        } else {
            header->type = onda_client_stream_type_unknown_m;
        }
    }

    uint32_t total_size = std_stack_used_size_from ( stack, begin );
    uint32_t stream_size = 0;
    while ( stream_size < total_size ) {
        uint32_t chunk_size = onda_stream_chunk_size_m;
        uint32_t remaining_size = total_size - stream_size;
        uint32_t write_size = remaining_size < chunk_size ? remaining_size : chunk_size;
        write_size = net->write_connected_socket ( client_socket, begin + stream_size, write_size );
        if ( write_size == 0 ) {
            std_log_warn_m ( "Streaming error " std_fmt_str_m, path );
            return false;
        }
        stream_size += write_size;
    }

    std_log_info_m ( "Finished streaming " std_fmt_str_m, path );
    return true;
}

typedef struct {
    uint32_t data_size;
    char data[];
} onda_client_request_t;

static void onda_server_drop_connection ( onda_server_connection_t* connection ) {
    net_i* net = onda_state->net;
    std_log_info_m ( "Dropping client " std_fmt_str_m ":" std_fmt_u16_m, connection->ip, connection->address.port );
    net->destroy_socket ( connection->socket );
    onda_server_remove_connection ( connection );
}

static std_app_state_e onda_update_server ( void ) {
    net_i* net = onda_state->net;

    net_socket_address_t new_client_address;
    net_socket_h new_client_socket = net->accept_pending_connection ( &new_client_address, onda_state->socket );
    if ( new_client_socket != net_null_handle_m ) {
        net->set_socket_is_blocking ( new_client_socket, true );
        onda_server_add_connection ( new_client_socket, new_client_address );
    }

    uint32_t served_clients_count = 0;
    uint64_t connection_idx = 0;
    while ( std_bitset_scan ( &connection_idx, onda_state->server.connections_bitset, connection_idx, std_bitset_u64_count_m ( onda_server_max_connections_m ) ) ) {
        onda_server_connection_t* connection = &onda_state->server.connections_array[connection_idx++];
        net_socket_h client_socket = connection->socket;
        
        if ( net->get_socket_available_read_size ( client_socket ) == 0 ) {
            continue;
        }

        ++served_clients_count;

        std_stack_t* stack = &onda_state->stack;
        std_stack_clear ( stack );
        std_auto_m request = std_stack_alloc_m ( stack, onda_client_request_t );
        size_t read_result = net->read_connected_socket ( request, sizeof ( onda_client_request_t ), client_socket, net_connected_socket_read_flag_read_all_m );
        if ( read_result == 0 ) {
            onda_server_drop_connection ( connection );
            continue;
        }

        char* client_msg = std_stack_alloc_zero ( stack, request->data_size );
        net->read_connected_socket ( client_msg, request->data_size, client_socket, net_connected_socket_read_flag_read_all_m );
        std_log_info_m ( "Received request from client " std_fmt_str_m ":" std_fmt_u16_m ": '" std_fmt_str_m "'", connection->ip, connection->address.port, client_msg );

        if ( std_str_starts_with ( client_msg, "list" ) ) {
            char* path = client_msg + 5;
            list_result_t list = onda_build_list ( stack, path );
            if ( !onda_server_send_list ( client_socket, stack, &list ) ) {
                onda_server_drop_connection ( connection );
            }
        } else if ( std_str_starts_with ( client_msg, "play" ) ) {
            char* client_path = client_msg + 5;
            std_string_t media_path = std_stack_alloc_string ( stack, onda_path_size_m );
            std_path_append_dir ( &media_path, onda_state->server.root_path.str );
            std_path_append_file ( &media_path, client_path );
            if ( !onda_server_stream_media ( client_socket, stack, media_path.str ) ) {
                onda_server_drop_connection ( connection );
            }
        } else if ( std_str_starts_with ( client_msg, "exit" ) ) {
            onda_server_drop_connection ( connection );
        } else {
            std_log_warn_m ( "Bad input from client " std_fmt_str_m ":" std_fmt_u16_m ": '" std_fmt_str_m "'", connection->ip, connection->address.port, client_msg );
            onda_server_drop_connection ( connection );
        }
    }

    if ( served_clients_count == 0 ) {
        std_thread_this_sleep ( 0 );
    }

    return std_app_state_tick_m;
}

typedef struct {
    bool paused;
    uint32_t total_size;    // total size of the stream
    uint32_t read_size;     // stream size that was read from network
    uint32_t consumed_size; // stream size that was consumed after being read
    void* begin;
    // TODO
    onda_client_stream_type_e type;
    union {
        mp3dec_t mp3dec;
        fx_flac_t* flac;
    };
} onda_client_stream_state_t;

static aud_source_h create_stream_source ( onda_client_stream_state_t* stream_state, const onda_media_header_t* header ) {
    aud_i* aud = onda_state->client.aud;
    aud_source_h source = aud_null_handle_m;

    std_assert_m ( header->channel_count == 2 );
    //std_assert_m ( header->bits_per_sample == 16 );
    aud_source_params_t params = {
        .sample_frequency = header->sample_frequency,
        .bits_per_sample = header->bits_per_sample,
        .sample_count = header->sample_count,
        .channel_count = header->channel_count,
    };
    source = aud->create_source ( &params );

    return source;
}

static list_result_t onda_client_read_list ( std_stack_t* stack ) {
    net_i* net = onda_state->net;
    net_socket_h socket = onda_state->socket;

    std_auto_m header = std_stack_alloc_m ( stack, onda_list_header_t );
    net->read_connected_socket ( header, sizeof ( onda_list_header_t ), socket, net_connected_socket_read_flag_read_all_m );

    uint32_t subdir_data_size = onda_subpath_size_m * header->subdir_count;
    uint32_t file_data_size = onda_subpath_size_m * header->file_count;
    char* subdir_data = std_stack_alloc ( stack, subdir_data_size );
    char* file_data = std_stack_alloc ( stack, file_data_size );
    net->read_connected_socket ( header->data, subdir_data_size + file_data_size, socket, net_connected_socket_read_flag_read_all_m );

    list_result_t list = {
        .subdir_count = header->subdir_count,
        .file_count = header->file_count,
    };
    list.subdirs = std_stack_alloc ( stack, subdir_data_size );
    list.files = std_stack_alloc ( stack, file_data_size );
    for ( uint32_t i = 0; i < list.subdir_count; ++i ) {
        list.subdirs[i] = subdir_data + onda_subpath_size_m * i;
    }
    for ( uint32_t i = 0; i < list.file_count; ++i ) {
        list.files[i] = file_data + onda_subpath_size_m * i;
    }

    return list;
}

static onda_media_header_t* onda_client_read_media_header ( std_stack_t* stack, onda_client_stream_state_t* state ) {
    net_i* net = onda_state->net;
    net_socket_h socket =  onda_state->socket;

    // read header
    std_stack_align ( stack, 8 );
    std_auto_m header = std_stack_alloc_m ( stack, onda_media_header_t );
    net->read_connected_socket ( header, sizeof ( onda_media_header_t ), socket, net_connected_socket_read_flag_read_all_m );

    // read label
    uint32_t label_size = header->label_size;
    char* label = std_stack_alloc ( stack, label_size );
    net->read_connected_socket ( label, label_size, socket, net_connected_socket_read_flag_read_all_m );

    // initialize stream state
    state->begin = std_stack_align ( stack, 8 );
    state->total_size = header->media_size;
    state->read_size = 0;
    state->consumed_size = 0;
    state->type = header->type;
    state->paused = false;

    return header;
}

static void* onda_client_read_media_chunk ( std_stack_t* stack, onda_client_stream_state_t* state ) {
    net_i* net = onda_state->net;
    net_socket_h socket =  onda_state->socket;

    size_t read_size = net->get_socket_available_read_size ( socket );
    if ( read_size == 0 ) {
        return NULL;
    }

    void* data = std_stack_alloc ( stack, read_size );
    net->read_connected_socket ( data, read_size, socket, net_connected_socket_read_flag_read_all_m );
    state->read_size += read_size;

    return data;
}

static void onda_client_feed_stream ( aud_source_h source, onda_client_stream_state_t* stream_state ) {
    aud_i* aud = onda_state->client.aud;
    std_stack_t* stack = &onda_state->stack;
    
    if ( stream_state->read_size < stream_state->total_size ) {
        onda_client_read_media_chunk ( stack, stream_state );
    }

    if ( stream_state->type == onda_client_stream_type_mp3_m ) {
        int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
        while ( stream_state->consumed_size < stream_state->read_size ) {
            void* decode_base = stream_state->begin + stream_state->consumed_size;
            uint32_t decode_size = stream_state->read_size - stream_state->consumed_size;
            // buffer in at least 16KB as requested by minimp3
            if ( decode_size < 1024 * 16 && stream_state->read_size != stream_state->total_size ) {
                break;
            }
            mp3dec_frame_info_t info;
            int samples = mp3dec_decode_frame ( &stream_state->mp3dec, decode_base, decode_size, pcm, &info );
            stream_state->consumed_size += info.frame_bytes;
            aud->feed_source ( source, pcm, sizeof ( int16_t ) * samples * info.channels );
        }
    } else if ( stream_state->type == onda_client_stream_type_flac_m ) {
        int32_t pcm[4096];
        while ( stream_state->consumed_size < stream_state->read_size ) {
            void* decode_base = stream_state->begin + stream_state->consumed_size;
            uint32_t decode_size = stream_state->read_size - stream_state->consumed_size;
            uint32_t sample_count = std_static_array_count_m ( pcm );
            fx_flac_state_t decode_result = fx_flac_process ( stream_state->flac, decode_base, &decode_size, pcm, &sample_count );
            if ( decode_result == FLAC_ERR ) {
                // TODO
                std_assert_m ( false );
            }
            stream_state->consumed_size += decode_size;
            aud->feed_source ( source, pcm, sizeof ( int32_t ) * sample_count );
        }
    } else if ( stream_state->type == onda_client_stream_type_unknown_m ) {
        std_log_error_m ( "Unknown stream type" );
    } else {
        std_log_error_m ( "Malformed stream type" );
    }
}

static void onda_client_decode_init ( onda_client_stream_state_t* stream_state ) {
    if ( stream_state->type == onda_client_stream_type_mp3_m ) {
        mp3dec_init ( &stream_state->mp3dec );
    } else if ( stream_state->type == onda_client_stream_type_flac_m ) {
        stream_state->flac = FX_FLAC_ALLOC_DEFAULT();
    } else if ( stream_state->type == onda_client_stream_type_unknown_m ) {
        std_log_error_m ( "Unknown stream type" );
    } else {
        std_log_error_m ( "Malformed stream type" );
    }
}

static bool onda_client_stream_source ( aud_source_h source, char* label, onda_client_stream_state_t* stream_state ) {
    aud_i* aud = onda_state->client.aud;

    aud->play_source ( source );
    aud->set_source_volume ( source, onda_state->client.volume );

    const std_ring_t* device_ring = aud->get_device_ring ( onda_state->client.device );
    uint64_t ring_capacity = std_ring_capacity ( device_ring );

    aud_device_info_t device_info;
    aud->get_device_info ( &device_info, onda_state->client.device );

    onda_client_decode_init ( stream_state );

    wm_i* wm = onda_state->client.wm;
    uint64_t step_ms = 50;
    std_tick_t frame_tick = std_tick_now();
    wm_input_state_t old_input_state;
    wm->get_window_input_state ( onda_state->client.window, &old_input_state );
    while ( true ) {
        // input
        wm->update_window ( onda_state->client.window );

        wm_window_info_t window_info;
        wm->get_window_info ( &window_info, onda_state->client.window );
        uint64_t sleep_ms = 0;
        if ( !window_info.is_focus ) {
            sleep_ms = step_ms / 2;
        }
        
        // left alt + up/down : volume
        // left alt + left/right : skip
        // left alt + left ctrl + up : pause/resume
        // left alt + left ctrl + down : stop
        wm_input_state_t new_input_state;
        wm->get_window_input_state ( onda_state->client.window, &new_input_state );
        if ( new_input_state.keyboard[wm_keyboard_state_alt_left_m] ) {
            if ( new_input_state.keyboard[wm_keyboard_state_ctrl_left_m] ) {
                if ( new_input_state.keyboard[wm_keyboard_state_up_m] && !old_input_state.keyboard[wm_keyboard_state_up_m] ) {
                    stream_state->paused = !stream_state->paused;
                    if ( stream_state->paused ) {
                        aud->pause_source ( source );
                    } else {
                        aud->play_source ( source );
                    }
                }
                if ( new_input_state.keyboard[wm_keyboard_state_down_m] && !old_input_state.keyboard[wm_keyboard_state_down_m] ) {
                    return false;
                }
            } else {                
                if ( new_input_state.keyboard[wm_keyboard_state_right_m] && !old_input_state.keyboard[wm_keyboard_state_right_m] ) {
                    if ( new_input_state.keyboard[wm_keyboard_state_shift_left_m] ) {
                        aud->skip_source ( source, 60 * 60 * 12 );
                    } else {
                        aud->skip_source ( source, 10 );
                    }
                }
                if ( new_input_state.keyboard[wm_keyboard_state_left_m] && !old_input_state.keyboard[wm_keyboard_state_left_m] ) {
                    if ( new_input_state.keyboard[wm_keyboard_state_shift_left_m] ) {
                        aud->skip_source ( source, -60 * 60 * 12 );
                    } else {
                        aud->skip_source ( source, -10 );
                    }
                }
                if ( new_input_state.keyboard[wm_keyboard_state_up_m] && !old_input_state.keyboard[wm_keyboard_state_up_m] ) {
                    onda_state->client.volume = std_min_f32 ( 1, onda_state->client.volume + 0.05 );
                    aud->set_source_volume ( source, onda_state->client.volume );
                }
                if ( new_input_state.keyboard[wm_keyboard_state_down_m] && !old_input_state.keyboard[wm_keyboard_state_down_m] ) {
                    onda_state->client.volume = std_max_f32 ( 0, onda_state->client.volume - 0.05 );
                    aud->set_source_volume ( source, onda_state->client.volume );
                }
            }
        }

        old_input_state = new_input_state;

        // time
        std_tick_t new_tick = std_tick_now();
        float delta_ms = std_tick_to_milli_f32 ( new_tick - frame_tick );
        if ( delta_ms < step_ms / 2 ) {
            std_thread_this_sleep ( sleep_ms );
            continue;
        }
        
        frame_tick = new_tick;

        // feed
        onda_client_feed_stream ( source, stream_state );

        // print
        aud_source_info_t source_info;
        aud->get_source_info ( &source_info, source );
        float total_duration = source_info.total_time;

        aud->update_device_ring ( onda_state->client.device );
        uint64_t ring_count = std_ring_count ( device_ring );

        uint32_t bar_size = 60;
        float bar_tick_duration = total_duration * 1000.f / bar_size;
        {
            char bar[100] = { 0 };
            size_t i = 0;

            bar[i++] = '[';

            uint64_t time_played_ms = ( uint64_t ) ( source_info.time_played * 1000.f );

            while ( time_played_ms > bar_tick_duration && i < bar_size ) {
                bar[i++] = '=';
                time_played_ms -= bar_tick_duration;
            }

            uint64_t streamed_ticks = ( float ) stream_state->read_size / stream_state->total_size * bar_size;

            while ( i < streamed_ticks && i < bar_size ) {
                bar[i++] = '-';
            }

            while ( i < bar_size ) {
                bar[i++] = ' ';
            }

            bar[i++] = ']';

            std_log_m ( std_log_level_custom_m, 
                std_fmt_clear_m std_fmt_clear_m std_fmt_str_m "\n" std_fmt_str_m " " std_fmt_u64_pad_m(2) "/" std_fmt_u64_m " | " std_fmt_f32_dec_m(2) "\n", 
                label, bar, ring_count, ring_capacity, onda_state->client.volume );
        }

        // output
        if ( source_info.frames_played < source_info.total_frames ) {
            if ( ring_count < ring_capacity && !stream_state->paused ) {
                aud->output_to_device ( onda_state->client.device, step_ms );
            }
        } else if ( ring_count == 0 && stream_state->read_size >= stream_state->total_size ) {
            break;
        }
        
        std_thread_this_sleep ( sleep_ms );
    }

    return true;
}

static void onda_client_print_list ( std_stack_t* stack, const list_result_t* list ) {
    void* begin = stack->top;
    std_stack_string_copy_format ( stack, std_fmt_u32_m " dirs\n", list->subdir_count );
    for ( uint32_t i = 0; i < list->subdir_count; ++i ) {
        std_stack_string_append_format ( stack, std_fmt_tab_m std_fmt_u32_m " - " std_fmt_str_m "\n", i, list->subdirs[i] );
    }

    std_stack_string_append_format ( stack, std_fmt_u32_m " files\n", list->file_count );
    for ( uint32_t i = 0; i < list->file_count; ++i ) {
        std_stack_string_append_format ( stack, std_fmt_tab_m std_fmt_u32_m " - " std_fmt_str_m "\n", i, list->files[i] );
    }

    std_log_m ( std_log_level_custom_m, std_fmt_str_m "\n", begin );
}

static void onda_client_request_list ( std_stack_t* stack, const char* path ) {
    net_i* net = onda_state->net;
    onda_client_request_t* request = std_stack_alloc_m ( stack, onda_client_request_t );
    std_string_t string = std_stack_alloc_string ( stack, onda_path_size_m + 5 );
    std_string_append ( &string, "list " );
    std_path_append_dir ( &string, onda_state->client.path.str );
    std_path_append_dir ( &string, path );
    uint32_t data_size = string.len + 1;
    request->data_size = data_size;
    net->write_connected_socket ( onda_state->socket, request, sizeof ( onda_client_request_t ) + data_size );
}

static void onda_client_flush_stream ( std_stack_t* stack, onda_client_stream_state_t* stream ) {
    net_i* net = onda_state->net;

    uint32_t read_size = stream->total_size - stream->read_size;
    if ( read_size == 0 ) {
        return;
    }

    void* data = std_stack_alloc ( stack, read_size );
    net->read_connected_socket ( data, read_size, onda_state->socket, net_connected_socket_read_flag_read_all_m );
}

static bool onda_client_play_media ( std_stack_t* stack, const char* path ) {
    net_i* net = onda_state->net;

    onda_client_request_t* request = std_stack_alloc_m ( stack, onda_client_request_t );
    std_string_t string = std_stack_alloc_string ( stack, onda_path_size_m + 5 );
    std_string_append ( &string, "play " );
    std_path_append_dir ( &string, onda_state->client.path.str );
    std_path_append_file ( &string, path );
    uint32_t request_data_size =  string.len + 1;
    request->data_size = request_data_size;
    net->write_connected_socket ( onda_state->socket, request, sizeof ( onda_client_request_t ) + request_data_size );

    aud_i* aud = onda_state->client.aud;

    bool exit = false;

    onda_client_stream_state_t stream_state;
    onda_media_header_t* media_header = onda_client_read_media_header ( stack, &stream_state );

    char* label = media_header->data;

    aud_source_h source = create_stream_source ( &stream_state, media_header );

    if ( source != aud_null_handle_m ) {
        if ( !onda_client_stream_source ( source, label, &stream_state ) ) {
            // TODO interrupt stream source
            std_log_m ( std_log_level_custom_m, "Flushing... " );
            onda_client_flush_stream ( stack, &stream_state );
            std_log_m ( std_log_level_custom_m, "done.\n" );
            exit = true;
        }
        aud->destroy_source ( source );
    }

    return exit;
}

static std_app_state_e onda_update_client ( void ) {
    net_i* net = onda_state->net;

    std_process_info_t process_info;
    std_process_info ( &process_info, std_process_this() );
    
    char cmd_buffer[32] = {};
    char* cmd = cmd_buffer;
    size_t cmd_size = 0;
    std_process_io_read ( cmd, &cmd_size, sizeof ( cmd_buffer ) - 1, process_info.io.stdin_handle );
    while ( cmd[cmd_size-1] == '\r' || cmd[cmd_size-1] == '\n' ) {
        cmd[--cmd_size] = '\0';
    }
    if ( cmd[cmd_size-1] != '\0' ) {
        cmd[cmd_size++] = '\0';
    }

    if ( std_str_cmp ( cmd, "reload" ) == 0 ) {
        return std_app_state_reload_m;
    }

    std_stack_t* stack = &onda_state->stack;
    std_stack_clear ( stack );

    std_stack_t* list_stack = &onda_state->client.list_stack;

    if ( std_str_cmp ( cmd, "list" ) == 0 ) {
        onda_client_request_list ( stack, "" );
        std_stack_clear ( list_stack );
        list_result_t list = onda_client_read_list ( list_stack );
        onda_state->client.list = list;
        onda_client_print_list ( stack, &list );
    } else if ( std_str_starts_with ( cmd, "open" ) ) {
        uint32_t id = std_str_to_u32 ( cmd + 5 );
        list_result_t* client_list = &onda_state->client.list;
        std_assert_m ( id < client_list->subdir_count + client_list->file_count );
        if ( id < client_list->subdir_count ) {
            std_path_append_dir ( &onda_state->client.path, client_list->subdirs[id] );
        } else {
            std_path_append_file ( &onda_state->client.path, client_list->files[id - client_list->subdir_count] );
        }
        onda_client_request_list ( stack, "" );
        std_stack_clear ( list_stack );
        list_result_t list = onda_client_read_list ( list_stack );
        onda_state->client.list = list;
        onda_client_print_list ( stack, &list );
    } else if ( std_str_cmp ( cmd, "back" ) == 0 ) {
        std_path_pop ( &onda_state->client.path );
        onda_client_request_list ( stack, "" );
        std_stack_clear ( list_stack );
        list_result_t list = onda_client_read_list ( list_stack );
        onda_state->client.list = list;
        onda_client_print_list ( stack, &list );
    } else if ( std_str_cmp ( cmd, "exit" ) == 0 ) {
        onda_client_request_t* request = std_stack_alloc_m ( stack, onda_client_request_t );
        request->data_size = cmd_size;
        std_stack_string_copy ( stack, cmd );
        net->write_connected_socket ( onda_state->socket, request, sizeof ( onda_client_request_t ) + cmd_size );
        net->destroy_socket ( onda_state->socket );
        return std_app_state_exit_m;
    } else if ( std_str_starts_with ( cmd, "play" ) ) {
        list_result_t* client_list = &onda_state->client.list;
        uint32_t id = std_str_to_u32 ( cmd + 5 );
        std_assert_m ( id < client_list->subdir_count + client_list->file_count );
        std_log_m ( std_log_level_custom_m, "\n\n" ); // make room for 2 empty lines for the player
        if ( id < client_list->subdir_count ) {
            std_path_append_dir ( &onda_state->client.path, client_list->subdirs[id] );
            onda_client_request_list ( stack, "" );
            list_result_t subdir_list = onda_client_read_list ( stack );
            onda_client_print_list ( stack, &subdir_list );
            for ( uint32_t i = 0; i < subdir_list.file_count; ++i ) {
                bool exit = onda_client_play_media ( stack, subdir_list.files[i] );
                if ( exit ) break;
            }
            std_path_pop ( &onda_state->client.path );
        } else {
            char* file = client_list->files[id - client_list->subdir_count];
            onda_client_play_media ( stack, file );
        }
    } else if ( std_str_cmp ( cmd, "help" ) == 0 ) {
        onda_client_print_help();
    } else {
        std_log_warn_m ( "Bad input: \"" std_fmt_str_m "\"", cmd );
        onda_client_request_t* request = std_stack_alloc_m ( stack, onda_client_request_t );
        cmd = "exit";
        cmd_size = sizeof ( "exit" );
        request->data_size = cmd_size;
        std_stack_string_copy ( stack, cmd );
        net->write_connected_socket ( onda_state->socket, request, sizeof ( onda_client_request_t ) + cmd_size );
        net->destroy_socket ( onda_state->socket );
        return std_app_state_exit_m;
    }

    return std_app_state_tick_m;
}

static std_app_state_e onda_update ( void ) {
    if ( onda_state->is_server ) {
        return onda_update_server();
    } else {
        return onda_update_client();
    }
}

std_app_state_e onda_tick ( void ) {
    if ( !onda_state->boot ) {
        if ( !onda_boot() ) {
            return std_app_state_exit_m;
        }
    }
    return onda_update();
}

std_module_export_m void* onda_load ( void* runtime ) {
    std_runtime_bind ( runtime );
    onda_state_t* state = onda_state_alloc();
    *state = onda_state_m (
        .api.tick = onda_tick,
        .net = std_module_load_m ( net_module_name_m ),
    );
    return state;
}

std_module_export_m void onda_unload ( void ) {
    // TODO
    std_module_unload_m ( net_module_name_m );
    if ( !onda_state->is_server ) {
        std_module_unload_m ( aud_module_name_m );
        std_module_unload_m ( wm_module_name_m );
    }
    onda_state_free();
}

std_module_export_m void onda_reload ( void* runtime, void* api ) {
    std_runtime_bind ( runtime );
    std_auto_m state = ( onda_state_t* ) api;
    state->api.tick = onda_tick;
    onda_state_bind ( state );
}
