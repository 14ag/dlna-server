#include "ssdp.h"
#include "config.h"
#include "dlna_utils.h"
#include "log.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <random>
#include <sstream>
#include <sys/select.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#ifndef IPV6_JOIN_GROUP
#define IPV6_JOIN_GROUP IPV6_ADD_MEMBERSHIP
#endif

namespace {
constexpr int kSsdpPort = 1900;
constexpr const char* kSsdpMulticastIPv4 = "239.255.255.250";
constexpr const char* kSsdpMulticastIPv6 = "ff02::c";
constexpr auto kAliveInterval = std::chrono::minutes(15);

struct SSDPTarget {
    std::string st;
    std::string usn;
};

unsigned int ComputeDelayMilliseconds(int mxSeconds) {
    int boundedSeconds = std::max(0, std::min(mxSeconds, 5));
    if (boundedSeconds <= 1) return 0;
    unsigned int maxDelay = static_cast<unsigned int>(boundedSeconds * 1000);
    if (maxDelay == 0) return 0;
    static thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<unsigned int> distribution(0, maxDelay);
    return distribution(generator);
}

std::vector<SSDPTarget> BuildTargets(const std::string& uuid) {
    return {
        {"upnp:rootdevice", "uuid:" + uuid + "::upnp:rootdevice"},
        {"uuid:" + uuid, "uuid:" + uuid},
        {"urn:schemas-upnp-org:device:MediaServer:1", "uuid:" + uuid + "::urn:schemas-upnp-org:device:MediaServer:1"},
        {"urn:schemas-upnp-org:service:ContentDirectory:1", "uuid:" + uuid + "::urn:schemas-upnp-org:service:ContentDirectory:1"},
        {"urn:schemas-upnp-org:service:ConnectionManager:1", "uuid:" + uuid + "::urn:schemas-upnp-org:service:ConnectionManager:1"},
    };
}

int CreateIPv4Socket(const std::vector<NetworkEndpoint>& endpoints) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
    unsigned char ttl = 2;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = htons(kSsdpPort);
    bindAddr.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) != 0) {
        close(fd);
        return -1;
    }
    for (const auto& endpoint : endpoints) {
        if (endpoint.family != AF_INET) continue;
        ip_mreq req{};
        inet_pton(AF_INET, kSsdpMulticastIPv4, &req.imr_multiaddr);
        req.imr_interface = reinterpret_cast<const sockaddr_in*>(&endpoint.sockaddr)->sin_addr;
        if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &req, sizeof(req)) == 0) {
            LogPrint(L"SSDP IPv4 multicast join ok: if=%lu addr=%hs", endpoint.interfaceIndex, endpoint.address.c_str());
        }
    }
    return fd;
}

int CreateIPv6Socket(const std::vector<NetworkEndpoint>& endpoints) {
    int fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes));
    int hops = 2;
    setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof(hops));

    sockaddr_in6 bindAddr{};
    bindAddr.sin6_family = AF_INET6;
    bindAddr.sin6_port = htons(kSsdpPort);
    bindAddr.sin6_addr = in6addr_any;
    if (bind(fd, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) != 0) {
        close(fd);
        return -1;
    }

    for (const auto& endpoint : endpoints) {
        if (endpoint.family != AF_INET6) continue;
        ipv6_mreq req{};
        inet_pton(AF_INET6, kSsdpMulticastIPv6, &req.ipv6mr_multiaddr);
        req.ipv6mr_interface = static_cast<unsigned int>(endpoint.interfaceIndex);
        if (setsockopt(fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, &req, sizeof(req)) == 0) {
            LogPrint(L"SSDP IPv6 multicast join ok: if=%lu addr=%hs", endpoint.interfaceIndex, endpoint.address.c_str());
        }
    }
    return fd;
}

void SetOutboundInterface(int fd, const NetworkEndpoint& endpoint) {
    if (endpoint.family == AF_INET) {
        in_addr addr = reinterpret_cast<const sockaddr_in*>(&endpoint.sockaddr)->sin_addr;
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &addr, sizeof(addr));
    } else if (endpoint.family == AF_INET6) {
        unsigned int ifIndex = static_cast<unsigned int>(endpoint.interfaceIndex);
        setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_IF, &ifIndex, sizeof(ifIndex));
    }
}

bool BuildMulticastDestination(const NetworkEndpoint& endpoint, sockaddr_storage& dest, socklen_t& destLen, std::string& hostHeader) {
    std::memset(&dest, 0, sizeof(dest));
    if (endpoint.family == AF_INET) {
        sockaddr_in dest4{};
        dest4.sin_family = AF_INET;
        dest4.sin_port = htons(kSsdpPort);
        inet_pton(AF_INET, kSsdpMulticastIPv4, &dest4.sin_addr);
        std::memcpy(&dest, &dest4, sizeof(dest4));
        destLen = sizeof(dest4);
        hostHeader = std::string(kSsdpMulticastIPv4) + ":" + std::to_string(kSsdpPort);
        return true;
    }
    if (endpoint.family == AF_INET6) {
        sockaddr_in6 dest6{};
        dest6.sin6_family = AF_INET6;
        dest6.sin6_port = htons(kSsdpPort);
        dest6.sin6_scope_id = static_cast<unsigned int>(endpoint.interfaceIndex);
        inet_pton(AF_INET6, kSsdpMulticastIPv6, &dest6.sin6_addr);
        std::memcpy(&dest, &dest6, sizeof(dest6));
        destLen = sizeof(dest6);
        hostHeader = "[ff02::c]:" + std::to_string(kSsdpPort);
        return true;
    }
    return false;
}
}

SSDP& SSDP::Get() {
    static SSDP instance;
    return instance;
}

SSDP::SSDP() : m_running(false), m_ipv4Socket(-1), m_ipv6Socket(-1), m_port(0) {
}

bool SSDP::Start(const std::vector<NetworkEndpoint>& endpoints, int port, const std::wstring& serverName, const std::wstring& uuid) {
    if (m_running.load()) return true;
    m_endpoints = endpoints;
    m_port = port;
    m_serverName = WideToUtf8(serverName);
    m_uuidStr = WideToUtf8(uuid);
    m_ipv4Socket = CreateIPv4Socket(m_endpoints);
    m_ipv6Socket = CreateIPv6Socket(m_endpoints);
    if (m_ipv4Socket < 0 && m_ipv6Socket < 0) return false;
    m_running.store(true);
    m_thread = std::thread(&SSDP::ThreadWorker, this);
    m_responseThread = std::thread(&SSDP::ResponseWorker, this);
    SendNotifyBurst("ssdp:alive", 3, 100);
    return true;
}

void SSDP::Stop() {
    if (!m_running.load()) return;
    m_running.store(false);
    SendNotifyBurst("ssdp:byebye", 1, 0);
    if (m_ipv4Socket >= 0) shutdown(m_ipv4Socket, SHUT_RDWR);
    if (m_ipv6Socket >= 0) shutdown(m_ipv6Socket, SHUT_RDWR);
    m_responseCondition.notify_all();
    if (m_thread.joinable()) m_thread.join();
    if (m_responseThread.joinable()) m_responseThread.join();
    CloseSockets();
}

void SSDP::CloseSockets() {
    if (m_ipv4Socket >= 0) {
        close(m_ipv4Socket);
        m_ipv4Socket = -1;
    }
    if (m_ipv6Socket >= 0) {
        close(m_ipv6Socket);
        m_ipv6Socket = -1;
    }
}

void SSDP::SendNotifyRound(const char* nts) {
    const std::string serverHeader = GetDlnaServerHeader();
    for (const auto& endpoint : m_endpoints) {
        int socketFd = endpoint.family == AF_INET ? m_ipv4Socket : m_ipv6Socket;
        if (socketFd < 0) continue;

        std::lock_guard<std::mutex> socketLock(m_socketMutex);
        sockaddr_storage dest{};
        socklen_t destLen = 0;
        std::string hostHeader;
        if (!BuildMulticastDestination(endpoint, dest, destLen, hostHeader)) continue;
        SetOutboundInterface(socketFd, endpoint);

        for (const auto& target : BuildTargets(m_uuidStr)) {
            std::stringstream ss;
            ss << "NOTIFY * HTTP/1.1\r\n"
               << "HOST: " << hostHeader << "\r\n";
            if (std::strcmp(nts, "ssdp:byebye") != 0) {
                ss << "CACHE-CONTROL: max-age=1800\r\n"
                   << "LOCATION: " << endpoint.locationUrl << "\r\n";
            }
            ss << "NT: " << target.st << "\r\n"
               << "NTS: " << nts << "\r\n";
            if (std::strcmp(nts, "ssdp:byebye") != 0) {
                ss << "SERVER: " << serverHeader << "\r\n";
            }
            ss << "USN: " << target.usn << "\r\n\r\n";
            const std::string msg = ss.str();
            sendto(socketFd, msg.data(), msg.size(), 0, reinterpret_cast<sockaddr*>(&dest), destLen);
        }
    }
}

void SSDP::SendNotifyBurst(const char* nts, int rounds, unsigned int delayMs) {
    for (int i = 0; i < rounds; ++i) {
        SendNotifyRound(nts);
        if (i + 1 < rounds && delayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
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
    if (!m_running.load()) return;
    std::lock_guard<std::mutex> socketLock(m_socketMutex);
    SetOutboundInterface(response.socket, response.endpoint);
    for (const std::string& message : response.messages) {
        sendto(response.socket,
               message.data(),
               message.size(),
               0,
               reinterpret_cast<const sockaddr*>(&response.remoteAddr),
               static_cast<socklen_t>(response.remoteLen));
    }
}

void SSDP::ResponseWorker() {
    while (true) {
        DelayedSearchResponse response{};
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
                m_responseCondition.wait_until(lock, next->dueAt);
                continue;
            }
            response = std::move(*next);
            m_delayedResponses.erase(next);
        }

        SendDelayedSearchResponse(response);
    }
}

void SSDP::HandleSearchRequest(int socketFd, const SOCKADDR* remoteAddr, socklen_t remoteLen, const std::string& request) {
    const size_t firstLineEnd = request.find("\r\n");
    if (firstLineEnd == std::string::npos || ToLowerAscii(request.substr(0, firstLineEnd)) != "m-search * http/1.1") return;
    const std::string man = ToLowerAscii(TrimAscii(FindHeaderValueCaseInsensitive(request, "MAN")));
    const std::string st = FindHeaderValueCaseInsensitive(request, "ST");
    const std::string mxText = FindHeaderValueCaseInsensitive(request, "MX");
    int mx = 1;
    if (!mxText.empty() && !TryParseIntStrict(TrimAscii(mxText), mx)) {
        mx = 1;
    }
    if (man != "\"ssdp:discover\"" && man != "ssdp:discover") return;
    const NetworkEndpoint* endpoint = SelectBestEndpoint(m_endpoints, remoteAddr);
    if (!endpoint || endpoint->family != remoteAddr->sa_family) return;

    std::vector<SSDPTarget> targets = BuildTargets(m_uuidStr);
    std::vector<SSDPTarget> responses;
    if (ToLowerAscii(st) == "ssdp:all") {
        responses = targets;
    } else {
        for (const auto& target : targets) {
            if (ToLowerAscii(target.st) == ToLowerAscii(st)) responses.push_back(target);
        }
    }
    unsigned int delayMs = ComputeDelayMilliseconds(mx);
    const std::string serverHeader = GetDlnaServerHeader();
    DelayedSearchResponse delayed{};
    delayed.socket = socketFd;
    delayed.remoteLen = static_cast<int>(remoteLen);
    delayed.endpoint = *endpoint;
    delayed.dueAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
    std::memcpy(&delayed.remoteAddr, remoteAddr, remoteLen);
    for (const auto& target : responses) {
        std::stringstream ss;
        ss << "HTTP/1.1 200 OK\r\n"
           << "CACHE-CONTROL: max-age=1800\r\n"
           << "DATE: " << BuildHttpDateHeaderValue() << "\r\n"
           << "EXT:\r\n"
           << "LOCATION: " << endpoint->locationUrl << "\r\n"
           << "SERVER: " << serverHeader << "\r\n"
           << "ST: " << target.st << "\r\n"
           << "USN: " << target.usn << "\r\n\r\n";
        delayed.messages.push_back(ss.str());
        delayed.logSt.push_back(target.st);
        delayed.logUsn.push_back(target.usn);
    }
    if (delayMs == 0) {
        SendDelayedSearchResponse(delayed);
    } else {
        QueueSearchResponses(std::move(delayed));
    }
}

void SSDP::ThreadWorker() {
    auto lastNotify = std::chrono::steady_clock::now();
    while (m_running.load()) {
        fd_set readfds;
        FD_ZERO(&readfds);
        int maxFd = -1;
        if (m_ipv4Socket >= 0) {
            FD_SET(m_ipv4Socket, &readfds);
            maxFd = std::max(maxFd, m_ipv4Socket);
        }
        if (m_ipv6Socket >= 0) {
            FD_SET(m_ipv6Socket, &readfds);
            maxFd = std::max(maxFd, m_ipv6Socket);
        }
        if (maxFd < 0) break;

        timeval tv{1, 0};
        int result = select(maxFd + 1, &readfds, nullptr, nullptr, &tv);
        if (!m_running.load()) break;

        auto now = std::chrono::steady_clock::now();
        if (now - lastNotify >= kAliveInterval) {
            SendNotifyRound("ssdp:alive");
            lastNotify = now;
        }

        if (result <= 0) continue;

        int sockets[] = { m_ipv4Socket, m_ipv6Socket };
        for (int socketFd : sockets) {
            if (socketFd < 0 || !FD_ISSET(socketFd, &readfds)) continue;
            char buffer[4096];
            sockaddr_storage remote{};
            socklen_t remoteLen = sizeof(remote);
            ssize_t bytes = recvfrom(socketFd, buffer, sizeof(buffer) - 1, 0, reinterpret_cast<sockaddr*>(&remote), &remoteLen);
            if (bytes <= 0) continue;
            buffer[bytes] = '\0';
            HandleSearchRequest(socketFd, reinterpret_cast<SOCKADDR*>(&remote), remoteLen, std::string(buffer, static_cast<size_t>(bytes)));
        }
    }
}
