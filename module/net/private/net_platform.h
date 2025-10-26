#pragma once

#include <net.h>

#if defined std_platform_win32_m
#include <winsock2.h>
#include <ws2tcpip.h>
#elif defined std_platform_linux_m
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#endif

void net_platform_init ( void );
void net_platform_shutdown ( void );
