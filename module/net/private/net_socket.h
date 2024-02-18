#pragma once

#include <net.h>

// https://www.tenouk.com/Winsock/Winsock2story.html

void net_socket_init ( void );

net_socket_h net_socket_create ( const net_socket_params_t* params );
bool net_socket_destroy ( net_socket_h socket );

bool net_socket_bind_address ( net_socket_h socket, const net_socket_address_t* address );
bool net_socket_connect ( net_socket_h socket, const net_socket_address_t* address );
bool net_socket_listen_for_connections ( net_socket_h socket );
net_socket_h net_socket_accept_pending_connection ( net_socket_address_t* address, net_socket_h socket );

size_t net_socket_get_available_read_size ( net_socket_h socket );
size_t read_connected_socket ( void* dest, size_t cap,  net_socket_h socket );
size_t write_connected_socket ( net_socket_h socket, const void* data, size_t size );
size_t read_socket ( net_socket_address_t* address, void* dest, size_t cap,  net_socket_h socket );
size_t write_socket ( net_socket_h socket, const net_socket_address_t* address, const void* data, size_t size );
