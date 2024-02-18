#pragma once

#include <net.h>

#include <winsock2.h>
#include <ws2tcpip.h>

void net_platform_init ( void );
void net_platform_shutdown ( void );
