#include "ssdp.h"
#include "config.h"
#include "log.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <sstream>
#include <sys/select.h>
#include <thread>
#include <unistd.h>

namespace {
constexpr int kSsdpPort = 1900;
constexpr const char* kSsdpMulticastIPv4 = "239.255.255.250";

struct SSDPTarget {
    std::string st;
    std::string usn;
};

std::string TrimAscii(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) ++start;
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(start, end - start);
}

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string HeaderValue(const std::string& request, const std::string& name) {
    const std::string needle = ToLowerAscii(name) + ":";
    size_t pos = request.find("\r\n");
    while (pos != std::string::npos) {
        pos += 2;
        const size_t end = request.find("\r\n", pos);
        if (end == std::string::npos || end == pos) break;
        const std::string line = request.substr(pos, end - pos);
        if (ToLowerAscii(line).rfind(needle, 0) == 0) return TrimAscii(line.substr(name.size() + 1));
        pos = end;
    }
    return {};
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

void SetIPv4OutboundInterface(int fd, const NetworkEndpoint& endpoint) {
    if (endpoint.family != AF_INET) return;
    in_addr addr = reinterpret_cast<const sockaddr_in*>(&endpoint.sockaddr)->sin_addr;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &addr, sizeof(addr));
}
}

SSDP& SSDP::Get() {
    static SSDP instance;
    return instance;
}

SSDP::SSDP() : m_running(false), m_ipv4Socket(-1), m_ipv6Socket(-1), m_port(0) {
}

bool SSDP::Start(const std::vector<NetworkEndpoint>& endpoints, int port, const std::wstring&, const std::wstring& uuid) {
    if (m_running) return true;
    m_endpoints = endpoints;
    m_port = port;
    m_uuidStr = WideToUtf8(uuid);
    m_ipv4Socket = CreateIPv4Socket(m_endpoints);
    if (m_ipv4Socket < 0) return false;
    m_running = true;
    m_thread = std::thread(&SSDP::ThreadWorker, this);
    SendNotifyBurst("ssdp:alive", 3, 100);
    return true;
}

void SSDP::Stop() {
    if (!m_running) return;
    m_running = false;
    SendNotifyBurst("ssdp:byebye", 1, 0);
    CloseSockets();
    if (m_thread.joinable()) m_thread.join();
}

void SSDP::CloseSockets() {
    if (m_ipv4Socket >= 0) {
        close(m_ipv4Socket);
        m_ipv4Socket = -1;
    }
}

void SSDP::SendNotifyRound(const char* nts) {
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(kSsdpPort);
    inet_pton(AF_INET, kSsdpMulticastIPv4, &dest.sin_addr);
    for (const auto& endpoint : m_endpoints) {
        if (endpoint.family != AF_INET || m_ipv4Socket < 0) continue;
        SetIPv4OutboundInterface(m_ipv4Socket, endpoint);
        for (const auto& target : BuildTargets(m_uuidStr)) {
            std::stringstream ss;
            ss << "NOTIFY * HTTP/1.1\r\n"
               << "HOST: " << kSsdpMulticastIPv4 << ":" << kSsdpPort << "\r\n";
            if (std::strcmp(nts, "ssdp:byebye") != 0) {
                ss << "CACHE-CONTROL: max-age=1800\r\n"
                   << "LOCATION: " << endpoint.locationUrl << "\r\n";
            }
            ss << "NT: " << target.st << "\r\n"
               << "NTS: " << nts << "\r\n";
            if (std::strcmp(nts, "ssdp:byebye") != 0) {
                ss << "SERVER: " << DLNA_PLATFORM_NAME << " UPnP/1.1 DLNAD/1.0\r\n";
            }
            ss << "USN: " << target.usn << "\r\n\r\n";
            const std::string msg = ss.str();
            sendto(m_ipv4Socket, msg.data(), msg.size(), 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
        }
    }
}

void SSDP::SendNotifyBurst(const char* nts, int rounds, unsigned int delayMs) {
    for (int i = 0; i < rounds; ++i) {
        SendNotifyRound(nts);
        if (i + 1 < rounds && delayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
}

void SSDP::HandleSearchRequest(int socketFd, const SOCKADDR* remoteAddr, socklen_t remoteLen, const std::string& request) {
    const size_t firstLineEnd = request.find("\r\n");
    if (firstLineEnd == std::string::npos || ToLowerAscii(request.substr(0, firstLineEnd)) != "m-search * http/1.1") return;
    const std::string man = ToLowerAscii(TrimAscii(HeaderValue(request, "MAN")));
    const std::string st = HeaderValue(request, "ST");
    if (man != "\"ssdp:discover\"" && man != "ssdp:discover") return;
    const NetworkEndpoint* endpoint = SelectBestEndpoint(m_endpoints, remoteAddr);
    if (!endpoint || endpoint->family != AF_INET) return;
    SetIPv4OutboundInterface(socketFd, *endpoint);
    std::vector<SSDPTarget> targets = BuildTargets(m_uuidStr);
    std::vector<SSDPTarget> responses;
    if (ToLowerAscii(st) == "ssdp:all") {
        responses = targets;
    } else {
        for (const auto& target : targets) {
            if (ToLowerAscii(target.st) == ToLowerAscii(st)) responses.push_back(target);
        }
    }
    for (const auto& target : responses) {
        std::stringstream ss;
        ss << "HTTP/1.1 200 OK\r\n"
           << "CACHE-CONTROL: max-age=1800\r\n"
           << "DATE: " << BuildHttpDateHeaderValue() << "\r\n"
           << "EXT:\r\n"
           << "LOCATION: " << endpoint->locationUrl << "\r\n"
           << "SERVER: " << DLNA_PLATFORM_NAME << " UPnP/1.1 DLNAD/1.0\r\n"
           << "ST: " << target.st << "\r\n"
           << "USN: " << target.usn << "\r\n\r\n";
        const std::string msg = ss.str();
        sendto(socketFd, msg.data(), msg.size(), 0, remoteAddr, remoteLen);
    }
}

void SSDP::ThreadWorker() {
    while (m_running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(m_ipv4Socket, &readfds);
        timeval tv{1, 0};
        int result = select(m_ipv4Socket + 1, &readfds, nullptr, nullptr, &tv);
        if (result <= 0 || !m_running) continue;
        char buffer[4096];
        sockaddr_storage remote{};
        socklen_t remoteLen = sizeof(remote);
        ssize_t bytes = recvfrom(m_ipv4Socket, buffer, sizeof(buffer) - 1, 0, reinterpret_cast<sockaddr*>(&remote), &remoteLen);
        if (bytes <= 0) continue;
        buffer[bytes] = '\0';
        HandleSearchRequest(m_ipv4Socket, reinterpret_cast<SOCKADDR*>(&remote), remoteLen, std::string(buffer, static_cast<size_t>(bytes)));
    }
}
