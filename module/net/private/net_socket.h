#pragma once

#include <net.h>

#include "net_platform.h"

#include <std_mutex.h>

typedef enum {
    net_socket_state_invalid_m,
    net_socket_state_unbound_m,
    net_socket_state_bound_m,
    net_socket_state_listening_m,
    net_socket_state_connected_m,
} net_socket_state_e;

typedef struct {
    net_socket_params_t params;
    net_socket_address_t address;
    //char address_string[net_address_string_size_m];
#if defined std_platform_win32_m
    SOCKET os_handle;
#elif defined std_platform_linux_m
    int os_handle;
#endif
    net_socket_state_e state;
} net_socket_t;

typedef struct {
    net_socket_t* sockets_array;
    net_socket_t* sockets_freelist;
    std_mutex_t sockets_mutex;
} net_socket_state_t;

void net_socket_load ( net_socket_state_t* state );
void net_socket_reload ( net_socket_state_t* state );
void net_socket_unload ( void );

net_socket_h net_socket_create ( const net_socket_params_t* params );
bool net_socket_destroy ( net_socket_h socket );

bool net_socket_bind_address ( net_socket_h socket, const net_socket_address_t* address );
bool net_socket_connect ( net_socket_h socket, const net_socket_address_t* address );
bool net_socket_listen_for_connections ( net_socket_h socket );
net_socket_h net_socket_accept_pending_connection ( net_socket_address_t* address, net_socket_h socket );

size_t net_socket_get_available_read_size ( net_socket_h socket );
size_t net_socket_read_connected ( void* dest, size_t cap, net_socket_h socket, net_connected_socket_read_flags_e flags );
size_t net_socket_write_connected ( net_socket_h socket, const void* data, size_t size );
size_t net_socket_read ( net_socket_address_t* address, void* dest, size_t cap,  net_socket_h socket );
size_t net_socket_write ( net_socket_h socket, const net_socket_address_t* address, const void* data, size_t size );
