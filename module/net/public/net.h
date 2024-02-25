#pragma once

#include <std_module.h>
#include <std_byte.h>
#include <std_platform.h>

#define net_module_name_m net
std_module_export_m void* net_load ( void* );
std_module_export_m void net_unload ( void );

#define net_null_handle_m UINT64_MAX

// Address
typedef enum {
    net_address_family_ip4_m,
    net_address_family_ip6_m,
} net_address_family_e;

#define net_address_ip4_string_size_m 16
#define net_address_ip6_string_size_m 64

#define net_address_ip4_bytes_size_m 4
#define net_address_ip6_bytes_size_m 16

#define net_address_bytes_size_m  net_address_ip6_bytes_size_m
#define net_address_string_size_m net_address_ip6_string_size_m

typedef struct {
    union {
        uint32_t u32[4]; // ipv4 -> u32[0]
        uint64_t u64[2]; // ipv6 -> u64[]
        char bytes[16];
    };
} net_address_bytes_t; // network byte order

// IP Protocol
typedef enum {
    net_ip_protocol_tcp_m,
    net_ip_protocol_udp_m
} net_ip_protocol_e;

typedef struct {
    net_address_family_e family;
    net_ip_protocol_e protocol;
    net_address_bytes_t address;
} net_host_info_t;

// Socket
typedef uint64_t net_socket_h;
typedef uint16_t net_socket_port_t;

typedef struct {
    net_address_bytes_t ip; // network byte order
    net_socket_port_t port; // host byte order
} net_socket_address_t;

typedef struct {
    net_address_family_e family;
    net_ip_protocol_e protocol;
    bool is_blocking;
} net_socket_params_t;

// API
typedef struct {
    net_socket_h    ( *create_socket )                      ( const net_socket_params_t* params );
    bool            ( *destroy_socket )                     ( net_socket_h socket );
    bool            ( *bind_socket )                        ( net_socket_h socket, const net_socket_address_t* address );
    bool            ( *connect_socket )                     ( net_socket_h socket, const net_socket_address_t* address );
    bool            ( *listen_for_connections )             ( net_socket_h socket );
    net_socket_h    ( *accept_pending_connection )          ( net_socket_address_t* address, net_socket_h socket );

    size_t          ( *get_socket_available_read_size )     ( net_socket_h socket );
    size_t          ( *read_connected_socket )              ( void* dest, size_t cap,  net_socket_h socket );
    size_t          ( *write_connected_socket )             ( net_socket_h socket, const void* data, size_t size );
    size_t          ( *read_socket )                        ( net_socket_address_t* address, void* dest, size_t cap,  net_socket_h socket );
    size_t          ( *write_socket )                       ( net_socket_h socket, const net_socket_address_t* address, const void* data, size_t size );

    bool            ( *ip_string_to_bytes )                 ( net_address_bytes_t* dest, const char* address, net_address_family_e family );
    bool            ( *ip_bytes_to_string )                 ( char* dest, const net_address_bytes_t* address, net_address_family_e family );
} net_i;
