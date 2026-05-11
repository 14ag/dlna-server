#ifndef SSDP_H
#define SSDP_H

#include <string>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <atomic>
#include <thread>
#endif
#include "netutils.h"

class SSDP {
public:
    static SSDP& Get();

    bool Start(const std::vector<NetworkEndpoint>& endpoints, int port, const std::wstring& serverName, const std::wstring& uuid);
    void Stop();

private:
    SSDP();

    void CloseSockets();
    void SendNotifyRound(const char* nts);
#ifdef _WIN32
    void SendNotifyBurst(const char* nts, int rounds, DWORD delayMs);
    void HandleSearchRequest(SOCKET socket, const SOCKADDR* remoteAddr, int remoteLen, const std::string& request);
    static DWORD WINAPI ThreadWorker(LPVOID lpParam);

    bool m_running;
    HANDLE m_hThread;
    SOCKET m_ipv4Socket;
    SOCKET m_ipv6Socket;
#else
    void SendNotifyBurst(const char* nts, int rounds, unsigned int delayMs);
    void HandleSearchRequest(int socket, const SOCKADDR* remoteAddr, socklen_t remoteLen, const std::string& request);
    void ThreadWorker();

    std::atomic<bool> m_running;
    std::thread m_thread;
    int m_ipv4Socket;
    int m_ipv6Socket;
#endif

    std::vector<NetworkEndpoint> m_endpoints;
    int m_port;
    std::string m_serverName;
    std::string m_uuidStr;
};

#endif // SSDP_H
