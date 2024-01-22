#include "net_socket.h"

#include "net_platform.h"
#include "net_address.h"

#include <std_log.h>
#include <std_list.h>
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
    SOCKET os_handle;
    net_socket_state_e state;
} net_socket_t;

typedef struct {
    net_socket_t* sockets_array;
    net_socket_t* sockets_freelist;
    std_mutex_t sockets_mutex;
} net_socket_state_t;

static net_socket_state_t net_socket_state;

/*
    https://docs.microsoft.com/en-us/windows/win32/winsock/sockaddr-2
    https://docs.microsoft.com/en-us/windows/win32/winsock/winsock-ioctls
*/

static void net_socket_address_ip4_to_winsock ( struct sockaddr_in* sockaddr, const net_socket_address_t* address ) {
    sockaddr->sin_family = AF_INET;
    sockaddr->sin_port = htons ( address->port );
    sockaddr->sin_addr.s_addr = address->ip.u32[0];
}

static void net_socket_address_ip6_to_winsock ( struct sockaddr_in6* sockaddr, const net_socket_address_t* address ) {
    sockaddr->sin6_family = AF_INET6;
    sockaddr->sin6_port = htons ( address->port );
    std_mem_copy ( sockaddr->sin6_addr.u.Byte, address->ip.bytes, 16 );
    sockaddr->sin6_flowinfo = 0;
    sockaddr->sin6_scope_id = 0; // TODO https://stackoverflow.com/questions/58600024/what-is-sin6-scope-id-for-the-ipv6-loopback-address
}

static void net_socket_address_ip4_from_winsock ( net_socket_address_t* address, const struct sockaddr_in* sockaddr ) {
    address->ip.u32[0] = sockaddr->sin_addr.s_addr;
    address->port = ntohs ( sockaddr->sin_port );
    std_assert_m ( sockaddr->sin_family == AF_INET );
}

static void net_socket_address_ip6_from_winsock ( net_socket_address_t* address, const struct sockaddr_in6* sockaddr ) {
    std_mem_copy ( address->ip.bytes, sockaddr->sin6_addr.u.Byte, 16 );
    address->port = ntohs ( sockaddr->sin6_port );
    std_assert_m ( sockaddr->sin6_family == AF_INET6 );
}

// --

void net_socket_init ( void ) {
    static net_socket_t sockets_array[net_socket_max_sockets_m];
    net_socket_state.sockets_array = sockets_array;
    net_socket_state.sockets_freelist = std_static_freelist_m ( sockets_array );
    std_mutex_init ( &net_socket_state.sockets_mutex );
}

net_socket_h net_socket_create ( const net_socket_params_t* params ) {
    int af = -1;
    int type = -1;
    int protocol = -1;

    switch ( params->family ) {
        case net_address_family_ip4_m:
            af = AF_INET;
            break;

        case net_address_family_ip6_m:
            af = AF_INET6;
            break;
    }

    switch ( params->protocol ) {
        case net_ip_protocol_tcp_m:
            type = SOCK_STREAM;
            protocol = IPPROTO_TCP;
            break;

        case net_ip_protocol_udp_m:
            type = SOCK_DGRAM;
            protocol = IPPROTO_UDP;
            break;
    }

    std_assert_m ( af != -1 && type != -1 && protocol != -1 );

    SOCKET win32_socket = socket ( af, type, protocol );
    std_assert_m ( win32_socket != INVALID_SOCKET );

    std_mutex_lock ( &net_socket_state.sockets_mutex );
    net_socket_t* sock = std_list_pop_m ( &net_socket_state.sockets_freelist );
    std_mutex_unlock ( &net_socket_state.sockets_mutex );

    sock->params = *params;
    sock->os_handle = win32_socket;
    sock->state = net_socket_state_unbound_m;
    std_mem_zero_m ( &sock->address );
    //std_mem_zero_m ( &sock->address_string );

    if ( !params->is_blocking ) {
        u_long mode = 1;
        int error = ioctlsocket ( win32_socket, ( long ) FIONBIO, &mode );
        std_assert_m ( !error );
    }

    return ( net_socket_h ) ( sock - net_socket_state.sockets_array );
}

bool net_socket_destroy ( net_socket_h socket_handle ) {
    std_mutex_lock ( &net_socket_state.sockets_mutex );
    net_socket_t* sock = &net_socket_state.sockets_array[ ( uint64_t ) socket_handle];

    if ( sock == NULL ) {
        std_mutex_unlock ( &net_socket_state.sockets_mutex );
        return false;
    }

    int error = closesocket ( sock->os_handle );

    if ( error == 0 ) {
        std_list_push ( &net_socket_state.sockets_freelist, sock );
    }

    std_mutex_unlock ( &net_socket_state.sockets_mutex );

    return error == 0;
}

// --

bool net_socket_bind_address ( net_socket_h socket_handle, const net_socket_address_t* address ) {
    net_socket_t* sock = &net_socket_state.sockets_array[ ( uint64_t ) socket_handle];
    std_assert_m ( sock != NULL );

    std_assert_m ( sock->state == net_socket_state_unbound_m );

    int error;

    switch ( sock->params.family ) {
        case net_address_family_ip4_m: {
            struct sockaddr_in sockaddr;
            net_socket_address_ip4_to_winsock ( &sockaddr, address );
            error = bind ( sock->os_handle, ( SOCKADDR* ) &sockaddr, sizeof ( sockaddr ) );
        }
        break;

        case net_address_family_ip6_m: {
            struct sockaddr_in6 sockaddr;
            net_socket_address_ip6_to_winsock ( &sockaddr, address );
            error = bind ( sock->os_handle, ( SOCKADDR* ) &sockaddr, sizeof ( sockaddr ) );
        }
        break;
    }

    if ( error ) {
        return false;
    }

    sock->address = *address;
    sock->state = net_socket_state_bound_m;

    return true;
}

bool net_socket_connect ( net_socket_h socket_handle, const net_socket_address_t* address ) {
    net_socket_t* sock = &net_socket_state.sockets_array[ ( uint64_t ) socket_handle];
    std_assert_m ( sock != NULL );

    std_assert_m ( sock->state == net_socket_state_bound_m );

    int error;

    switch ( sock->params.family ) {
        case net_address_family_ip4_m: {
            struct sockaddr_in sockaddr;
            net_socket_address_ip4_to_winsock ( &sockaddr, address );
            error = connect ( sock->os_handle, ( SOCKADDR* ) &sockaddr, sizeof ( sockaddr ) );
        }
        break;

        case net_address_family_ip6_m: {
            struct sockaddr_in6 sockaddr;
            net_socket_address_ip6_to_winsock ( &sockaddr, address );
            error = connect ( sock->os_handle, ( SOCKADDR* ) &sockaddr, sizeof ( sockaddr ) );
        }
        break;
    }

    if ( error ) {
        return false;
    }

    sock->state = net_socket_state_connected_m;

    return true;
}

bool net_socket_listen_for_connections ( net_socket_h socket_handle ) {
    net_socket_t* sock = &net_socket_state.sockets_array[ ( uint64_t ) socket_handle];
    std_assert_m ( sock != NULL );

    std_assert_m ( sock->state == net_socket_state_bound_m );

    int error = listen ( sock->os_handle, SOMAXCONN );

    if ( error ) {
        return false;
    }

    sock->state = net_socket_state_listening_m;

    return true;
}

net_socket_h net_socket_accept_pending_connection ( net_socket_address_t* address, net_socket_h socket_handle ) {
    net_socket_t* sock = &net_socket_state.sockets_array[ ( uint64_t ) socket_handle];
    std_assert_m ( sock != NULL );

    std_assert_m ( sock->state == net_socket_state_listening_m );

    std_mem_zero_m ( address );
    SOCKADDR_STORAGE sockaddr;
    int sockaddr_size = sizeof ( sockaddr );
    SOCKET connection_socket = accept ( sock->os_handle, ( SOCKADDR* ) &sockaddr, &sockaddr_size );

    if ( connection_socket == INVALID_SOCKET ) {
        return false;
    }

    net_address_family_e family = net_address_family_from_winsock ( sockaddr.ss_family );

    net_socket_address_t connection_address;

    switch ( family ) {
        case net_address_family_ip4_m: {
            struct sockaddr_in* sockaddr4 = ( struct sockaddr_in* ) &sockaddr;
            net_socket_address_ip4_from_winsock ( &connection_address, sockaddr4 );
        }
        break;

        case net_address_family_ip6_m: {
            struct sockaddr_in6* sockaddr6 = ( struct sockaddr_in6* ) &sockaddr;
            net_socket_address_ip6_from_winsock ( &connection_address, sockaddr6 );
        }
        break;

        default:
            return net_null_handle_m;
    }

    std_mutex_lock ( &net_socket_state.sockets_mutex );
    net_socket_t* connection_sock = std_list_pop_m ( &net_socket_state.sockets_freelist );
    std_mutex_unlock ( &net_socket_state.sockets_mutex );

    connection_sock->params.family = sock->params.family;
    connection_sock->params.protocol = sock->params.protocol;
    connection_sock->params.is_blocking = true;
    connection_sock->address = connection_address;
    connection_sock->os_handle = connection_socket;
    connection_sock->state = net_socket_state_connected_m;

    *address = connection_address;

    net_socket_h connection_socket_handle = ( net_socket_h ) ( connection_sock - net_socket_state.sockets_array );
    return connection_socket_handle;
}

// --

size_t net_socket_get_available_read_size ( net_socket_h socket_handle ) {
    net_socket_t* sock = &net_socket_state.sockets_array[ ( uint64_t ) socket_handle];
    std_assert_m ( sock != NULL );

    std_assert_m ( sock->state >= net_socket_state_bound_m );

    u_long size;
    int error = ioctlsocket ( sock->os_handle, ( long ) FIONREAD, &size );
    std_assert_m ( !error );

    return size;
}

size_t read_connected_socket ( void* dest, size_t cap,  net_socket_h socket_handle ) {
    net_socket_t* sock = &net_socket_state.sockets_array[ ( uint64_t ) socket_handle];
    std_assert_m ( sock != NULL );

    std_assert_m ( sock->state == net_socket_state_connected_m );

    int read_size = recv ( sock->os_handle, dest, ( int ) cap, 0 );

    if ( read_size == SOCKET_ERROR ) {
        // TODO
        return 0;
    }

    return ( size_t ) read_size;
}

size_t write_connected_socket ( net_socket_h socket_handle, const void* data, size_t size ) {
    net_socket_t* sock = &net_socket_state.sockets_array[ ( uint64_t ) socket_handle];
    std_assert_m ( sock != NULL );

    std_assert_m ( sock->state == net_socket_state_connected_m );

    int write_size = send ( sock->os_handle, data, ( int ) size, 0 );

    if ( write_size == SOCKET_ERROR ) {
        // TODO
        return 0;
    }

    return ( size_t ) write_size;
}

size_t read_socket ( net_socket_address_t* address, void* dest, size_t cap, net_socket_h socket_handle ) {
    net_socket_t* sock = &net_socket_state.sockets_array[ ( uint64_t ) socket_handle];
    std_assert_m ( sock != NULL );

    std_assert_m ( sock->state >= net_socket_state_bound_m );

    std_mem_zero_m ( address );
    int read_size;
    SOCKADDR_STORAGE sockaddr;
    int sockaddr_size = sizeof ( sockaddr );
    read_size = recvfrom ( sock->os_handle, dest, ( int ) cap, 0, ( SOCKADDR* ) &sockaddr, &sockaddr_size );

    if ( read_size == SOCKET_ERROR ) {
        int error = WSAGetLastError();
        std_unused_m ( error );
        return false;
    }

    net_address_family_e family = net_address_family_from_winsock ( sockaddr.ss_family );

    switch ( family ) {
        case net_address_family_ip4_m: {
            struct sockaddr_in* sockaddr4 = ( struct sockaddr_in* ) &sockaddr;
            net_socket_address_ip4_from_winsock ( address, sockaddr4 );
        }
        break;

        case net_address_family_ip6_m: {
            struct sockaddr_in6* sockaddr6 = ( struct sockaddr_in6* ) &sockaddr;
            net_socket_address_ip6_from_winsock ( address, sockaddr6 );
        }
        break;
    }

    return ( size_t ) read_size;
}

size_t write_socket ( net_socket_h socket_handle, const net_socket_address_t* address, const void* data, size_t size ) {
    net_socket_t* sock = &net_socket_state.sockets_array[ ( uint64_t ) socket_handle];
    std_assert_m ( sock != NULL );

    std_assert_m ( sock->state >= net_socket_state_bound_m );

    int write_size;

    switch ( sock->params.family ) {
        case net_address_family_ip4_m: {
            struct sockaddr_in sockaddr;
            net_socket_address_ip4_to_winsock ( &sockaddr, address );
            write_size = sendto ( sock->os_handle, data, ( int ) size, 0, ( SOCKADDR* ) &sockaddr, sizeof ( sockaddr ) );
        }
        break;

        case net_address_family_ip6_m: {
            struct sockaddr_in6 sockaddr;
            net_socket_address_ip6_to_winsock ( &sockaddr, address );
            write_size = sendto ( sock->os_handle, data, ( int ) size, 0, ( SOCKADDR* ) &sockaddr, sizeof ( sockaddr ) );
        }
        break;
    }

    if ( write_size == SOCKET_ERROR ) {
        // TODO
        return 0;
    }

    return ( size_t ) write_size;
}
