#ifndef NETUTILS_INTERNAL_H
#define NETUTILS_INTERNAL_H
#ifdef _WIN32
#include <winsock2.h>
#include <string>
std::string BuildEndpointHost(const SOCKADDR* addr);
#endif
#endif