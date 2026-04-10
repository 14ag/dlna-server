#ifndef SSDP_H
#define SSDP_H

#include <string>
#include <winsock2.h>

class SSDP {
public:
    static SSDP& Get();

    bool Start(const std::string& ipAddress, int port, const std::wstring& serverName, const std::wstring& uuid);
    void Stop();

private:
    SSDP();
    
    void SendNotify(const char* type);
    static DWORD WINAPI ThreadWorker(LPVOID lpParam);

    bool m_running;
    HANDLE m_hThread;
    SOCKET m_socket;
    
    std::string m_ip;
    int m_port;
    std::string m_serverName;
    std::string m_uuidStr;
};

#endif // SSDP_H
