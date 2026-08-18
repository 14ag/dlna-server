#include "ssdp.h"
#include "ssdp_common.h"
#include "config.h"
#include "dlna_utils.h"
#include "log.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
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

#ifndef IP_UNICAST_IF
// value from the upstream linux kernel patch that introduced this option
// see include/linux/in.h in torvalds/linux commit 76e21053b5bf
// not always exposed by netinet in,h on every libc version this project targets
#define IP_UNICAST_IF 50
#endif
#ifndef IPV6_UNICAST_IF
// fallback definition for older headers that predate this constant
// value 76 is the stable linux kernel option number for this option
// it is the ipv6 counterpart of ip unicast if added for the wine project
// see the workflow document citations for the kernel commit reference
#define IPV6_UNICAST_IF 76
#endif

namespace {
constexpr int kSsdpPort = 1900;
constexpr const char* kSsdpMulticastIPv4 = "239.255.255.250";
constexpr const char* kSsdpMulticastIPv6 = "ff02::c";

constexpr size_t kMaxDelayedResponses = 256;

void DiscoveryLog(const wchar_t* fmt, ...) {
    if (!AppConfig.Snapshot().debugLog) {
        return;
    }
    wchar_t buffer[2048];
    va_list args;
    va_start(args, fmt);
    vswprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), fmt, args);
    va_end(args);
    LogPrint(L"%ls", buffer);
}

int CreateIPv4Socket(const std::vector<NetworkEndpoint>& endpoints) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
    unsigned char ttl = 4;
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
    int hops = 4;
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

// selects the outbound interface for a multicast destined send
// used only by SendNotifyRound for the periodic alive and byebye notify
// never call this before a unicast destined send see the function below
bool SetMulticastOutboundInterface(int fd, const NetworkEndpoint& endpoint) {
    if (endpoint.family == AF_INET) {
        in_addr addr = reinterpret_cast<const sockaddr_in*>(&endpoint.sockaddr)->sin_addr;
        if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &addr, sizeof(addr)) != 0) {
            LogPrint(L"IP_MULTICAST_IF failed for interface %lu.", endpoint.interfaceIndex);
            return false;
        }
    } else if (endpoint.family == AF_INET6) {
        unsigned int ifIndex = static_cast<unsigned int>(endpoint.interfaceIndex);
        if (setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_IF, &ifIndex, sizeof(ifIndex)) != 0) {
            LogPrint(L"IP_MULTICAST_IF failed for interface %lu.", endpoint.interfaceIndex);
            return false;
        }
    }
    return true;
}

// selects the outbound interface for a unicast destined send
// used only by SendDelayedSearchResponse for the m search reply
// ip multicast if has no defined effect on a unicast destination
// per the linux ip 7 man page so it must never be used here
// ip unicast if pins the real outbound interface where the running
// kernel defines it and is a best effort call everywhere else so a
// platform without this option still gets correct default kernel
// routing for the unicast destination instead of a wrong pin
// this function always returns true unlike the multicast one above
// because a failed or unavailable pin still leaves a fully working
// send behind through normal kernel routing while returning false
// here would turn a soft hint into a hard failure on the one path
// every discovering client actually depends on
bool SetUnicastOutboundInterface(int fd, const NetworkEndpoint& endpoint) {
    if (endpoint.family == AF_INET) {
#ifdef IP_UNICAST_IF
        unsigned int ifIndex = static_cast<unsigned int>(endpoint.interfaceIndex);
        setsockopt(fd, IPPROTO_IP, IP_UNICAST_IF, &ifIndex, sizeof(ifIndex));
#else
        (void)fd;
        (void)endpoint;
#endif
    } else if (endpoint.family == AF_INET6) {
#ifdef IPV6_UNICAST_IF
        unsigned int ifIndex = static_cast<unsigned int>(endpoint.interfaceIndex);
        setsockopt(fd, IPPROTO_IPV6, IPV6_UNICAST_IF, &ifIndex, sizeof(ifIndex));
#else
        (void)fd;
        (void)endpoint;
#endif
    }
    return true;
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

SSDP::SSDP() : m_running(false), m_ipv4Socket(-1), m_ipv6Socket(-1) {
}

bool SSDP::Start(const std::vector<NetworkEndpoint>& endpoints, int port, const std::wstring& serverName, const std::wstring& uuid) {
    (void)port;
    (void)serverName;
    bool expectedIdle = false;
    if (!m_lifecycleBusy.compare_exchange_strong(expectedIdle, true, std::memory_order_acq_rel)) {
        LogPrint(L"SSDP Start rejected: a Start or Stop call is already in progress.");
        return false;
    }
    struct LifecycleGuard {
        std::atomic<bool>& flag;
        ~LifecycleGuard() { flag.store(false, std::memory_order_release); }
    } lifecycleGuard{ m_lifecycleBusy };

    if (m_running.load()) return true;
    m_endpoints = endpoints;
    m_uuidStr = WideToUtf8(uuid);
    m_targets = BuildAdvertisedTargets(m_uuidStr);
    if (!m_bootIdAssigned) {
        m_bootId = static_cast<unsigned int>(time(nullptr));
        m_bootIdAssigned = true;
    }
    // CONFIGID.UPNP.ORG stays fixed across every restart in this
    // process this server's description.xml and SCPD documents never
    // change at runtime so there is nothing for a bumped CONFIGID to
    // signal and bumping it would force every control point to
    // needlessly re fetch and re cache those documents on every
    // transient network triggered restart see UDA 1 1 section 1 2
    m_configId = 1;
    m_ipv4Socket = CreateIPv4Socket(m_endpoints);
    m_ipv6Socket = CreateIPv6Socket(m_endpoints);
    if (m_ipv4Socket < 0 && m_ipv6Socket < 0) return false;
    m_running.store(true);
    m_workerThreadAlive.store(true, std::memory_order_release);
    m_thread = std::thread(&SSDP::ThreadWorker, this);
    m_responseThread = std::thread(&SSDP::ResponseWorker, this);
    // fire the initial ssdp alive burst asynchronously so Start does
    // not block the caller for the jitter delay plus three send rounds
    // mirrors the windows implementation in ssdp cpp SSDP Start see
    // that file for the full rationale comment
    // this thread is joined in Stop before CloseSockets runs
    // it must not be detached or it can read m_ipv4Socket m_ipv6Socket
    // at the same time Stop is writing them during teardown
    m_initialBurstThread = std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(ComputeSsdpStartupJitterMilliseconds()));
        // mirrors the windows side change in ssdp cpp see that file for
        // the full rationale comment
        SendNotifyBurst("ssdp:alive", 5, 150);
    });
    return true;
}

void SSDP::Stop() {
    bool expectedIdle = false;
    if (!m_lifecycleBusy.compare_exchange_strong(expectedIdle, true, std::memory_order_acq_rel)) {
        LogPrint(L"SSDP Stop rejected: a Start or Stop call is already in progress.");
        return;
    }
    struct LifecycleGuard {
        std::atomic<bool>& flag;
        ~LifecycleGuard() { flag.store(false, std::memory_order_release); }
    } lifecycleGuard{ m_lifecycleBusy };

    if (!m_running.load()) return;
    m_running.store(false);
    SendNotifyBurst("ssdp:byebye", 1, 0);
    if (m_ipv4Socket >= 0) shutdown(m_ipv4Socket, SHUT_RDWR);
    if (m_ipv6Socket >= 0) shutdown(m_ipv6Socket, SHUT_RDWR);
    m_responseCondition.notify_all();
    if (m_thread.joinable()) m_thread.join();
    if (m_responseThread.joinable()) m_responseThread.join();
    if (m_initialBurstThread.joinable()) m_initialBurstThread.join();
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

        std::mutex& sendMutex = (endpoint.family == AF_INET) ? m_ipv4SendMutex : m_ipv6SendMutex;
        std::lock_guard<std::mutex> socketLock(sendMutex);
        sockaddr_storage dest{};
        socklen_t destLen = 0;
        std::string hostHeader;
        if (!BuildMulticastDestination(endpoint, dest, destLen, hostHeader)) continue;
        if (!SetMulticastOutboundInterface(socketFd, endpoint)) {
            continue;
        }

        for (const auto& target : m_targets) {
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
            ss << "USN: " << target.usn << "\r\n"
               << "BOOTID.UPNP.ORG: " << m_bootId << "\r\n"
               << "CONFIGID.UPNP.ORG: " << m_configId << "\r\n"
               << "OPT: \"http://schemas.upnp.org/upnp/1/0/\"; ns=01\r\n"
               << "01-NLS: " << m_bootId << "\r\n\r\n";
            const std::string msg = ss.str();
            ssize_t sent = sendto(socketFd, msg.data(), msg.size(), 0, reinterpret_cast<sockaddr*>(&dest), destLen);
            if (sent < 0) {
                LogPrint(L"SSDP send failed while sending notify.");
            } else {
                DiscoveryLog(L"SSDP notify sent: nts=%hs target=%hs location=%hs if=%lu", nts, target.st.c_str(), endpoint.locationUrl.c_str(), endpoint.interfaceIndex);
            }
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
    if (response.messages.empty()) return;
    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        if (CoalesceDelayedResponse(m_delayedResponses, std::move(response))) {
            return;
        }
        if (m_delayedResponses.size() >= kMaxDelayedResponses) {
            m_delayedResponses.pop_front();
        }
        m_delayedResponses.push_back(std::move(response));
    }
    m_responseCondition.notify_one();
}

void SSDP::SendDelayedSearchResponse(const DelayedSearchResponse& response) {
    if (!m_running.load()) return;
    std::mutex& sendMutex = (response.endpoint.family == AF_INET) ? m_ipv4SendMutex : m_ipv6SendMutex;
    std::lock_guard<std::mutex> socketLock(sendMutex);
    if (!SetUnicastOutboundInterface(response.socket, response.endpoint)) return;
    for (size_t i = 0; i < response.messages.size(); ++i) {
        const std::string& message = response.messages[i];
        ssize_t sent = sendto(response.socket,
                              message.data(),
                              message.size(),
                              0,
                              reinterpret_cast<const sockaddr*>(&response.remoteAddr),
                              static_cast<socklen_t>(response.remoteLen));
        const std::string st = i < response.logSt.size() ? response.logSt[i] : std::string();
        const std::string usn = i < response.logUsn.size() ? response.logUsn[i] : std::string();
        std::string destination = SockaddrToLiteral(reinterpret_cast<const SOCKADDR*>(&response.remoteAddr));
        if (sent < 0) {
            LogPrint(L"SSDP send failed while sending search response.");
        } else {
            DiscoveryLog(L"SSDP response sent: dst=%hs st=%hs usn=%hs location=%hs", destination.c_str(), st.c_str(), usn.c_str(), response.endpoint.locationUrl.c_str());
        }
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
    if (firstLineEnd == std::string::npos) {
        DiscoveryLog(L"SSDP request ignored: malformed start line");
        return;
    }
    std::string firstLine = request.substr(0, firstLineEnd);
    if (ToLowerAscii(firstLine) != "m-search * http/1.1") {
        DiscoveryLog(L"SSDP request ignored: start line=%hs", firstLine.c_str());
        return;
    }
    const std::string manRaw = FindHeaderValueCaseInsensitive(request, "MAN");
    const std::string st = FindHeaderValueCaseInsensitive(request, "ST");
    const std::string mxText = FindHeaderValueCaseInsensitive(request, "MX");
    int mx = 1;
    if (!mxText.empty() && !TryParseIntStrict(TrimAscii(mxText), mx)) {
        mx = 1;
    }
    std::string source = SockaddrToLiteral(remoteAddr);

    DiscoveryLog(L"SSDP search in: src=%hs st=%hs mx=%d man=%hs", source.c_str(), st.c_str(), mx, manRaw.c_str());

    const std::string man = ToLowerAscii(TrimAscii(manRaw));
    if (man != "\"ssdp:discover\"" && man != "ssdp:discover") {
        DiscoveryLog(L"SSDP search ignored: invalid MAN from %hs", source.c_str());
        return;
    }
    const NetworkEndpoint* endpoint = SelectBestEndpoint(m_endpoints, remoteAddr);
    if (!endpoint || endpoint->family != remoteAddr->sa_family) {
        DiscoveryLog(L"SSDP search ignored: no endpoint match for %hs", source.c_str());
        return;
    }

    const std::vector<SSDPTarget>& targets = m_targets;
    std::vector<SSDPTarget> responses;
    if (ToLowerAscii(st) == "ssdp:all") {
        responses = targets;
    } else {
        for (const auto& target : targets) {
            if (ToLowerAscii(target.st) == ToLowerAscii(st)) responses.push_back(target);
        }
    }
    if (responses.empty()) {
        DiscoveryLog(L"SSDP search ignored: unsupported ST=%hs", st.c_str());
        return;
    }
    unsigned int delayMs = ComputeDelayMilliseconds(mx);
    DiscoveryLog(L"SSDP search match: src=%hs delayMs=%lu location=%hs", source.c_str(), static_cast<unsigned long>(delayMs), endpoint->locationUrl.c_str());
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
           << "USN: " << target.usn << "\r\n"
           << "BOOTID.UPNP.ORG: " << m_bootId << "\r\n"
           << "CONFIGID.UPNP.ORG: " << m_configId << "\r\n\r\n";
        delayed.messages.push_back(ss.str());
        delayed.logSt.push_back(target.st);
        delayed.logUsn.push_back(target.usn);
        DiscoveryLog(L"SSDP response queued: dst=%hs st=%hs usn=%hs location=%hs", source.c_str(), target.st.c_str(), target.usn.c_str(), endpoint->locationUrl.c_str());
    }
    if (delayMs == 0) {
        SendDelayedSearchResponse(delayed);
    } else {
        QueueSearchResponses(std::move(delayed));
    }
}

void SSDP::ThreadWorker() {
    auto lastNotify = std::chrono::steady_clock::now();
    auto nextAliveInterval = std::chrono::milliseconds(ComputeSsdpNextAliveIntervalMilliseconds());
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
        if (now - lastNotify >= nextAliveInterval) {
            SendNotifyRound("ssdp:alive");
            lastNotify = now;
            nextAliveInterval = std::chrono::milliseconds(ComputeSsdpNextAliveIntervalMilliseconds());
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
    m_workerThreadAlive.store(false, std::memory_order_release);
}
