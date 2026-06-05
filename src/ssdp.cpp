#include "ssdp.h"
#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include "netutils.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <algorithm>
#include <cctype>
#include <stdarg.h>
#include <vector>

#define SSDP_PORT 1900
#define SSDP_MULTICAST_IPV4 "239.255.255.250"
#define SSDP_MULTICAST_IPV6 "ff02::c"

namespace {
struct SSDPTarget {
    std::string st;
    std::string usn;
};

void DiscoveryLog(const wchar_t* fmt, ...) {
    if (!AppConfig.debugLog) {
        return;
    }

    wchar_t buffer[2048];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buffer, 2048, _TRUNCATE, fmt, args);
    va_end(args);
    LogPrint(L"%ls", buffer);
}

bool IsDiscoverManHeader(const std::string& man) {
    std::string normalized = ToLowerAscii(TrimAscii(man));
    return normalized == "\"ssdp:discover\"" || normalized == "ssdp:discover";
}

DWORD ComputeDelayMilliseconds(int mxSeconds) {
    int boundedSeconds = (std::max)(0, (std::min)(mxSeconds, 5));
    if (boundedSeconds <= 1) {
        return 0;
    }
    DWORD maxDelay = static_cast<DWORD>(boundedSeconds * 1000);
    if (maxDelay == 0) {
        return 0;
    }

    LARGE_INTEGER counter = {};
    QueryPerformanceCounter(&counter);
    DWORD randomValue = counter.LowPart ^ static_cast<DWORD>(GetTickCount64()) ^ GetCurrentThreadId();

    return randomValue % (maxDelay + 1);
}

bool SetOutboundInterface(SOCKET socket, const NetworkEndpoint& endpoint, bool multicast) {
    if (endpoint.family == AF_INET) {
        if (multicast) {
            const SOCKADDR_IN* addr4 = reinterpret_cast<const SOCKADDR_IN*>(&endpoint.sockaddr);
            in_addr localAddr = addr4->sin_addr;
            return setsockopt(socket,
                              IPPROTO_IP,
                              IP_MULTICAST_IF,
                              reinterpret_cast<const char*>(&localAddr),
                              sizeof(localAddr)) == 0;
        }

        DWORD ifIndex = htonl(endpoint.interfaceIndex);
        return setsockopt(socket,
                          IPPROTO_IP,
                          IP_UNICAST_IF,
                          reinterpret_cast<const char*>(&ifIndex),
                          sizeof(ifIndex)) == 0;
    }

    DWORD ifIndex = endpoint.interfaceIndex;
    return setsockopt(socket,
                      IPPROTO_IPV6,
                      multicast ? IPV6_MULTICAST_IF : IPV6_UNICAST_IF,
                      reinterpret_cast<const char*>(&ifIndex),
                      sizeof(ifIndex)) == 0;
}

std::vector<SSDPTarget> BuildAdvertisedTargets(const std::string& uuid) {
    std::vector<SSDPTarget> targets;
    targets.push_back({ "upnp:rootdevice", "uuid:" + uuid + "::upnp:rootdevice" });
    targets.push_back({ "uuid:" + uuid, "uuid:" + uuid });
    targets.push_back({ "urn:schemas-upnp-org:device:MediaServer:1", "uuid:" + uuid + "::urn:schemas-upnp-org:device:MediaServer:1" });
    targets.push_back({ "urn:schemas-upnp-org:service:ContentDirectory:1", "uuid:" + uuid + "::urn:schemas-upnp-org:service:ContentDirectory:1" });
    targets.push_back({ "urn:schemas-upnp-org:service:ConnectionManager:1", "uuid:" + uuid + "::urn:schemas-upnp-org:service:ConnectionManager:1" });
    return targets;
}

const SSDPTarget* FindTarget(const std::vector<SSDPTarget>& targets, const std::string& requestedSt) {
    std::string lowered = ToLowerAscii(requestedSt);
    for (const auto& target : targets) {
        if (ToLowerAscii(target.st) == lowered) {
            return &target;
        }
    }
    return NULL;
}
}

SSDP& SSDP::Get() {
    static SSDP instance;
    return instance;
}

SSDP::SSDP()
    : m_running(false),
      m_hThread(NULL),
      m_ipv4Socket(INVALID_SOCKET),
      m_ipv6Socket(INVALID_SOCKET),
      m_port(0) {
}

bool SSDP::Start(const std::vector<NetworkEndpoint>& endpoints, int port, const std::wstring& serverName, const std::wstring& uuid) {
    if (m_running.load()) return true;

    m_endpoints = endpoints;
    m_port = port;
    m_serverName = WideToUtf8(serverName);
    m_uuidStr = WideToUtf8(uuid);

    m_ipv4Socket = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, 0);
    if (m_ipv4Socket != INVALID_SOCKET) {
        BOOL reuse = TRUE;
        setsockopt(m_ipv4Socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        sockaddr_in localAddr = {};
        localAddr.sin_family = AF_INET;
        localAddr.sin_port = htons(SSDP_PORT);
        localAddr.sin_addr.s_addr = INADDR_ANY;

        if (bind(m_ipv4Socket, reinterpret_cast<SOCKADDR*>(&localAddr), sizeof(localAddr)) == SOCKET_ERROR) {
            DiscoveryLog(L"SSDP IPv4 bind failed: %d", WSAGetLastError());
            closesocket(m_ipv4Socket);
            m_ipv4Socket = INVALID_SOCKET;
        } else {
            for (const auto& endpoint : m_endpoints) {
                if (endpoint.family != AF_INET) {
                    continue;
                }

                ip_mreq membership = {};
                if (InetPtonA(AF_INET, SSDP_MULTICAST_IPV4, &membership.imr_multiaddr) != 1) {
                    DiscoveryLog(L"SSDP IPv4 multicast address parse failed");
                    continue;
                }
                membership.imr_interface = reinterpret_cast<const SOCKADDR_IN*>(&endpoint.sockaddr)->sin_addr;

                if (setsockopt(m_ipv4Socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&membership), sizeof(membership)) == 0) {
                    DiscoveryLog(L"SSDP IPv4 multicast join ok: if=%lu addr=%hs", endpoint.interfaceIndex, endpoint.address.c_str());
                } else {
                    DiscoveryLog(L"SSDP IPv4 multicast join failed: if=%lu addr=%hs err=%d", endpoint.interfaceIndex, endpoint.address.c_str(), WSAGetLastError());
                }
            }
        }
    }

    m_ipv6Socket = WSASocketW(AF_INET6, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, 0);
    if (m_ipv6Socket != INVALID_SOCKET) {
        BOOL reuse = TRUE;
        setsockopt(m_ipv6Socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        DWORD v6Only = 1;
        setsockopt(m_ipv6Socket, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6Only), sizeof(v6Only));

        sockaddr_in6 localAddr6 = {};
        localAddr6.sin6_family = AF_INET6;
        localAddr6.sin6_port = htons(SSDP_PORT);
        localAddr6.sin6_addr = in6addr_any;

        if (bind(m_ipv6Socket, reinterpret_cast<SOCKADDR*>(&localAddr6), sizeof(localAddr6)) == SOCKET_ERROR) {
            DiscoveryLog(L"SSDP IPv6 bind failed: %d", WSAGetLastError());
            closesocket(m_ipv6Socket);
            m_ipv6Socket = INVALID_SOCKET;
        } else {
            for (const auto& endpoint : m_endpoints) {
                if (endpoint.family != AF_INET6) {
                    continue;
                }

                ipv6_mreq membership6 = {};
                inet_pton(AF_INET6, SSDP_MULTICAST_IPV6, &membership6.ipv6mr_multiaddr);
                membership6.ipv6mr_interface = endpoint.interfaceIndex;

                if (setsockopt(m_ipv6Socket, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&membership6), sizeof(membership6)) == 0) {
                    DiscoveryLog(L"SSDP IPv6 multicast join ok: if=%lu addr=%hs", endpoint.interfaceIndex, endpoint.address.c_str());
                } else {
                    DiscoveryLog(L"SSDP IPv6 multicast join failed: if=%lu addr=%hs err=%d", endpoint.interfaceIndex, endpoint.address.c_str(), WSAGetLastError());
                }
            }
        }
    }

    if (m_ipv4Socket == INVALID_SOCKET && m_ipv6Socket == INVALID_SOCKET) {
        CloseSockets();
        return false;
    }

    m_running.store(true);
    m_hThread = CreateThread(NULL, 0, ThreadWorker, this, 0, NULL);
    if (!m_hThread) {
        m_running.store(false);
        CloseSockets();
        return false;
    }
    m_responseThread = std::thread(&SSDP::ResponseWorker, this);

    SendNotifyBurst("ssdp:alive", 3, 100);
    return true;
}

void SSDP::Stop() {
    if (!m_running.load()) return;

    m_running.store(false);
    SendNotifyBurst("ssdp:byebye", 1, 0);
    m_responseCondition.notify_all();

    if (m_hThread) {
        WaitForSingleObject(m_hThread, 2000);
        CloseHandle(m_hThread);
        m_hThread = NULL;
    }
    if (m_responseThread.joinable()) {
        m_responseThread.join();
    }
    CloseSockets();
}

void SSDP::CloseSockets() {
    if (m_ipv4Socket != INVALID_SOCKET) {
        closesocket(m_ipv4Socket);
        m_ipv4Socket = INVALID_SOCKET;
    }

    if (m_ipv6Socket != INVALID_SOCKET) {
        closesocket(m_ipv6Socket);
        m_ipv6Socket = INVALID_SOCKET;
    }
}

void SSDP::SendNotifyRound(const char* nts) {
    std::vector<SSDPTarget> targets = BuildAdvertisedTargets(m_uuidStr);
    std::string serverHeader = GetDlnaServerHeader();

    for (const auto& endpoint : m_endpoints) {
        SOCKET socket = (endpoint.family == AF_INET) ? m_ipv4Socket : m_ipv6Socket;
        if (socket == INVALID_SOCKET) {
            continue;
        }

        if (!SetOutboundInterface(socket, endpoint, true)) {
            DiscoveryLog(L"SSDP notify interface select failed: family=%d if=%lu err=%d", endpoint.family, endpoint.interfaceIndex, WSAGetLastError());
            continue;
        }

        sockaddr_storage dest = {};
        int destLen = 0;
        std::string hostHeader;

        if (endpoint.family == AF_INET) {
            sockaddr_in dest4 = {};
            dest4.sin_family = AF_INET;
            dest4.sin_port = htons(SSDP_PORT);
            inet_pton(AF_INET, SSDP_MULTICAST_IPV4, &dest4.sin_addr);
            memcpy(&dest, &dest4, sizeof(dest4));
            destLen = sizeof(dest4);
            hostHeader = SSDP_MULTICAST_IPV4;
        } else {
            sockaddr_in6 dest6 = {};
            dest6.sin6_family = AF_INET6;
            dest6.sin6_port = htons(SSDP_PORT);
            dest6.sin6_scope_id = endpoint.interfaceIndex;
            inet_pton(AF_INET6, SSDP_MULTICAST_IPV6, &dest6.sin6_addr);
            memcpy(&dest, &dest6, sizeof(dest6));
            destLen = sizeof(dest6);
            hostHeader = "[FF02::C]";
        }

        for (const auto& target : targets) {
            std::string message;
            if (_stricmp(nts, "ssdp:byebye") == 0) {
                message =
                    "NOTIFY * HTTP/1.1\r\n"
                    "HOST: " + hostHeader + ":" + std::to_string(SSDP_PORT) + "\r\n" +
                    "NT: " + target.st + "\r\n" +
                    "NTS: " + nts + "\r\n" +
                    "USN: " + target.usn + "\r\n"
                    "\r\n";
            } else {
                message =
                    "NOTIFY * HTTP/1.1\r\n"
                    "HOST: " + hostHeader + ":" + std::to_string(SSDP_PORT) + "\r\n"
                    "CACHE-CONTROL: max-age=1800\r\n"
                    "LOCATION: " + endpoint.locationUrl + "\r\n"
                    "NT: " + target.st + "\r\n" +
                    "NTS: " + nts + "\r\n"
                    "SERVER: " + serverHeader + "\r\n" +
                    "USN: " + target.usn + "\r\n"
                    "\r\n";
            }

            sendto(socket, message.c_str(), static_cast<int>(message.size()), 0, reinterpret_cast<const SOCKADDR*>(&dest), destLen);
            DiscoveryLog(L"SSDP notify sent: nts=%hs target=%hs location=%hs if=%lu", nts, target.st.c_str(), endpoint.locationUrl.c_str(), endpoint.interfaceIndex);
        }
    }
}

void SSDP::SendNotifyBurst(const char* nts, int rounds, DWORD delayMs) {
    for (int i = 0; i < rounds; ++i) {
        SendNotifyRound(nts);
        if (i + 1 < rounds && delayMs > 0) {
            Sleep(delayMs);
        }
    }
}

void SSDP::QueueSearchResponses(DelayedSearchResponse response) {
    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_delayedResponses.push_back(std::move(response));
    }
    m_responseCondition.notify_one();
}

void SSDP::SendDelayedSearchResponse(const DelayedSearchResponse& response) {
    if (!m_running.load()) {
        return;
    }
    if (!SetOutboundInterface(response.socket, response.endpoint, false)) {
        DiscoveryLog(L"SSDP response interface select failed: if=%lu err=%d", response.endpoint.interfaceIndex, WSAGetLastError());
        return;
    }

    for (size_t i = 0; i < response.messages.size(); ++i) {
        const std::string& message = response.messages[i];
        sendto(response.socket,
               message.c_str(),
               static_cast<int>(message.size()),
               0,
               reinterpret_cast<const SOCKADDR*>(&response.remoteAddr),
               response.remoteLen);
        const std::string st = i < response.logSt.size() ? response.logSt[i] : std::string();
        const std::string usn = i < response.logUsn.size() ? response.logUsn[i] : std::string();
        std::string destination = SockaddrToLiteral(reinterpret_cast<const SOCKADDR*>(&response.remoteAddr));
        DiscoveryLog(L"SSDP response sent: dst=%hs st=%hs usn=%hs location=%hs", destination.c_str(), st.c_str(), usn.c_str(), response.endpoint.locationUrl.c_str());
    }
}

void SSDP::ResponseWorker() {
    while (true) {
        DelayedSearchResponse response = {};
        {
            std::unique_lock<std::mutex> lock(m_responseMutex);
            while (m_running.load() && m_delayedResponses.empty()) {
                m_responseCondition.wait(lock);
            }
            if (!m_running.load()) {
                m_delayedResponses.clear();
                break;
            }

            auto next = std::min_element(m_delayedResponses.begin(), m_delayedResponses.end(),
                [](const DelayedSearchResponse& a, const DelayedSearchResponse& b) {
                    return a.dueAt < b.dueAt;
                });
            auto now = std::chrono::steady_clock::now();
            if (next->dueAt > now) {
                m_responseCondition.wait_for(lock, std::chrono::milliseconds(50));
                continue;
            }

            response = std::move(*next);
            m_delayedResponses.erase(next);
        }

        SendDelayedSearchResponse(response);
    }
}

void SSDP::HandleSearchRequest(SOCKET socket, const SOCKADDR* remoteAddr, int remoteLen, const std::string& request) {
    size_t firstLineEnd = request.find("\r\n");
    if (firstLineEnd == std::string::npos) {
        DiscoveryLog(L"SSDP request ignored: malformed start line");
        return;
    }

    std::string firstLine = request.substr(0, firstLineEnd);
    if (ToLowerAscii(firstLine) != "m-search * http/1.1") {
        DiscoveryLog(L"SSDP request ignored: start line=%hs", firstLine.c_str());
        return;
    }

    std::string man = FindHeaderValueCaseInsensitive(request, "MAN");
    std::string st = FindHeaderValueCaseInsensitive(request, "ST");
    std::string mxStr = FindHeaderValueCaseInsensitive(request, "MX");
    int mx = mxStr.empty() ? 1 : atoi(mxStr.c_str());
    std::string source = SockaddrToLiteral(remoteAddr);

    DiscoveryLog(L"SSDP search in: src=%hs st=%hs mx=%d man=%hs", source.c_str(), st.c_str(), mx, man.c_str());

    if (!IsDiscoverManHeader(man)) {
        DiscoveryLog(L"SSDP search ignored: invalid MAN from %hs", source.c_str());
        return;
    }

    const NetworkEndpoint* endpoint = SelectBestEndpoint(m_endpoints, remoteAddr);
    if (!endpoint) {
        DiscoveryLog(L"SSDP search ignored: no endpoint match for %hs", source.c_str());
        return;
    }

    std::vector<SSDPTarget> targets = BuildAdvertisedTargets(m_uuidStr);
    std::vector<const SSDPTarget*> responses;
    if (_stricmp(st.c_str(), "ssdp:all") == 0) {
        for (const auto& target : targets) {
            responses.push_back(&target);
        }
    } else {
        const SSDPTarget* match = FindTarget(targets, st);
        if (!match) {
            DiscoveryLog(L"SSDP search ignored: unsupported ST=%hs", st.c_str());
            return;
        }
        responses.push_back(match);
    }

    DWORD delayMs = ComputeDelayMilliseconds(mx);
    DiscoveryLog(L"SSDP search match: src=%hs delayMs=%lu location=%hs", source.c_str(), delayMs, endpoint->locationUrl.c_str());

    std::string date = BuildHttpDateHeaderValue();
    std::string serverHeader = GetDlnaServerHeader();
    DelayedSearchResponse delayed = {};
    delayed.socket = socket;
    delayed.remoteLen = remoteLen;
    delayed.endpoint = *endpoint;
    delayed.dueAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
    memcpy(&delayed.remoteAddr, remoteAddr, remoteLen);
    for (const SSDPTarget* target : responses) {
        std::string response =
            "HTTP/1.1 200 OK\r\n"
            "CACHE-CONTROL: max-age=1800\r\n"
            "DATE: " + date + "\r\n"
            "EXT:\r\n"
            "LOCATION: " + endpoint->locationUrl + "\r\n"
            "SERVER: " + serverHeader + "\r\n"
            "ST: " + target->st + "\r\n"
            "USN: " + target->usn + "\r\n"
            "\r\n";

        delayed.messages.push_back(response);
        delayed.logSt.push_back(target->st);
        delayed.logUsn.push_back(target->usn);
        DiscoveryLog(L"SSDP response queued: dst=%hs st=%hs usn=%hs location=%hs", source.c_str(), target->st.c_str(), target->usn.c_str(), endpoint->locationUrl.c_str());
    }
    if (delayMs == 0) {
        SendDelayedSearchResponse(delayed);
    } else {
        QueueSearchResponses(std::move(delayed));
    }
}

DWORD WINAPI SSDP::ThreadWorker(LPVOID lpParam) {
    SSDP* pThis = reinterpret_cast<SSDP*>(lpParam);
    ULONGLONG lastNotifyTicks = GetTickCount64();

    while (pThis->m_running.load()) {
        fd_set readfds;
        FD_ZERO(&readfds);

        int socketCount = 0;
        if (pThis->m_ipv4Socket != INVALID_SOCKET) {
            FD_SET(pThis->m_ipv4Socket, &readfds);
            ++socketCount;
        }
        if (pThis->m_ipv6Socket != INVALID_SOCKET) {
            FD_SET(pThis->m_ipv6Socket, &readfds);
            ++socketCount;
        }
        if (socketCount == 0) {
            break;
        }

        timeval tv = { 1, 0 };
        int result = select(0, &readfds, NULL, NULL, &tv);
        if (!pThis->m_running.load()) {
            break;
        }

        ULONGLONG now = GetTickCount64();
        if (now - lastNotifyTicks > 900000) {
            pThis->SendNotifyRound("ssdp:alive");
            lastNotifyTicks = now;
        }

        if (result <= 0) {
            continue;
        }

        SOCKET sockets[] = { pThis->m_ipv4Socket, pThis->m_ipv6Socket };
        for (SOCKET socket : sockets) {
            if (socket == INVALID_SOCKET || !FD_ISSET(socket, &readfds)) {
                continue;
            }

            char buffer[4096];
            sockaddr_storage senderAddr = {};
            int senderAddrSize = sizeof(senderAddr);
            int bytesRead = recvfrom(socket, buffer, sizeof(buffer) - 1, 0, reinterpret_cast<SOCKADDR*>(&senderAddr), &senderAddrSize);
            if (bytesRead <= 0) {
                continue;
            }

            buffer[bytesRead] = '\0';
            pThis->HandleSearchRequest(socket, reinterpret_cast<const SOCKADDR*>(&senderAddr), senderAddrSize, std::string(buffer, bytesRead));
        }
    }

    return 0;
}
