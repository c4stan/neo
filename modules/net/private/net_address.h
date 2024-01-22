#pragma once

#include <net.h>

#include "net_platform.h"

// bytes is in big-endian network byte order.
bool net_address_ip_string_to_bytes ( net_address_bytes_t* dest, const char* address, net_address_family_e family );
bool net_address_ip_bytes_to_string ( char* dest, const net_address_bytes_t* address, net_address_family_e family );

ADDRESS_FAMILY net_address_family_to_winsock ( net_address_family_e family );
net_address_family_e net_address_family_from_winsock ( ADDRESS_FAMILY family );
