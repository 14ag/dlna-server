#include "ssdp.h"
#include "log.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>

#define SSDP_MULTICAST_IPv4 "239.255.255.250"
#define SSDP_PORT 1900

SSDP& SSDP::Get() {
    static SSDP instance;
    return instance;
}

SSDP::SSDP() : m_running(false), m_hThread(NULL), m_socket(INVALID_SOCKET), m_port(0) {
}

bool SSDP::Start(const std::string& ipAddress, int port, const std::wstring& serverName, const std::wstring& uuid) {
    if (m_running) return true;

    m_ip = ipAddress;
    m_port = port;
    m_uuidStr = std::string(uuid.begin(), uuid.end());
    m_serverName = std::string(serverName.begin(), serverName.end());

    m_socket = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (m_socket == INVALID_SOCKET) {
        return false;
    }

    BOOL reuse = TRUE;
    setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    sockaddr_in localAddr;
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(SSDP_PORT);
    localAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(m_socket, (SOCKADDR*)&localAddr, sizeof(localAddr)) == SOCKET_ERROR) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(SSDP_MULTICAST_IPv4);
    mreq.imr_interface.s_addr = inet_addr(m_ip.c_str());
    if (setsockopt(m_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq)) == SOCKET_ERROR) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    m_running = true;
    m_hThread = CreateThread(NULL, 0, ThreadWorker, this, 0, NULL);
    
    SendNotify("ssdp:alive");
    SendNotify("ssdp:alive"); // Burst

    return true;
}

void SSDP::Stop() {
    if (!m_running) return;
    
    m_running = false;
    SendNotify("ssdp:byebye");

    if (m_socket != INVALID_SOCKET) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }

    if (m_hThread) {
        WaitForSingleObject(m_hThread, 2000);
        CloseHandle(m_hThread);
        m_hThread = NULL;
    }
}

void SSDP::SendNotify(const char* type) {
    if (m_socket == INVALID_SOCKET) return;

    sockaddr_in destAddr;
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(SSDP_PORT);
    destAddr.sin_addr.s_addr = inet_addr(SSDP_MULTICAST_IPv4);

    const char* targets[] = {
        "upnp:rootdevice",
        "urn:schemas-upnp-org:device:MediaServer:1",
        "urn:schemas-upnp-org:service:ContentDirectory:1",
        "urn:schemas-upnp-org:service:ConnectionManager:1"
    };

    for (int i = 0; i < 4; i++) {
        char msg[1024];
        sprintf_s(msg, sizeof(msg),
            "NOTIFY * HTTP/1.1\r\n"
            "HOST: %s:%d\r\n"
            "CACHE-CONTROL: max-age=1800\r\n"
            "LOCATION: http://%s:%d/description.xml\r\n"
            "NT: %s\r\n"
            "NTS: %s\r\n"
            "SERVER: Windows/10.0 UPnP/1.0 WinDLNAServer/1.0\r\n"
            "USN: uuid:%s::%s\r\n"
            "\r\n",
            SSDP_MULTICAST_IPv4, SSDP_PORT,
            m_ip.c_str(), m_port,
            targets[i], type,
            m_uuidStr.c_str(), targets[i]);

        sendto(m_socket, msg, (int)strlen(msg), 0, (SOCKADDR*)&destAddr, sizeof(destAddr));
    }
}

DWORD WINAPI SSDP::ThreadWorker(LPVOID lpParam) {
    SSDP* pThis = (SSDP*)lpParam;
    char buffer[2048];
    
    DWORD lastNotifyTicks = GetTickCount();

    while (pThis->m_running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(pThis->m_socket, &readfds);

        struct timeval tv = { 1, 0 }; // 1 sec timeout
        int result = select(0, &readfds, NULL, NULL, &tv);

        if (!pThis->m_running) break;

        // Periodic notify every 900 seconds
        if (GetTickCount() - lastNotifyTicks > 900000) {
            pThis->SendNotify("ssdp:alive");
            lastNotifyTicks = GetTickCount();
        }

        if (result > 0) {
            sockaddr_in senderAddr;
            int senderAddrSize = sizeof(senderAddr);
            int bytesRead = recvfrom(pThis->m_socket, buffer, sizeof(buffer) - 1, 0, (SOCKADDR*)&senderAddr, &senderAddrSize);
            
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                std::string req(buffer);
                
                if (req.find("M-SEARCH") == 0) {
                    bool match = false;
                    std::string st;

                    if (req.find("ssdp:all") != std::string::npos) { match = true; st = "ssdp:all"; }
                    else if (req.find("upnp:rootdevice") != std::string::npos) { match = true; st = "upnp:rootdevice"; }
                    else if (req.find("urn:schemas-upnp-org:device:MediaServer:1") != std::string::npos) { match = true; st = "urn:schemas-upnp-org:device:MediaServer:1"; }

                    if (match) {
                        // Normally would parse 'MX' and delay, omitted for simplicity
                        char resp[1024];
                        sprintf_s(resp, sizeof(resp),
                            "HTTP/1.1 200 OK\r\n"
                            "CACHE-CONTROL: max-age=1800\r\n"
                            "DATE: Thu, 01 Jan 1970 00:00:00 GMT\r\n"
                            "EXT:\r\n"
                            "LOCATION: http://%s:%d/description.xml\r\n"
                            "SERVER: Windows/10.0 UPnP/1.0 WinDLNAServer/1.0\r\n"
                            "ST: %s\r\n"
                            "USN: uuid:%s::%s\r\n"
                            "\r\n",
                            pThis->m_ip.c_str(), pThis->m_port,
                            (st == "ssdp:all" ? "urn:schemas-upnp-org:device:MediaServer:1" : st.c_str()),
                            pThis->m_uuidStr.c_str(),
                            (st == "ssdp:all" ? "urn:schemas-upnp-org:device:MediaServer:1" : st.c_str()));
                        
                        sendto(pThis->m_socket, resp, (int)strlen(resp), 0, (SOCKADDR*)&senderAddr, senderAddrSize);
                    }
                }
            }
        }
    }
    return 0;
}
