#include "netchange_watch.h"
#include "network_change_debounce.h"
#include "log.h"

#include <cstring>
#include <thread>
#include <atomic>
#include <functional>

#ifdef __linux__
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#endif

namespace {
#ifdef __linux__
int g_netlinkFd = -1;
int g_wakeupReadFd = -1;
int g_wakeupWriteFd = -1;
std::thread g_thread;
std::atomic<bool> g_running(false);
std::function<void()> g_onChange;
NetworkChangeDebouncer g_debounce(std::chrono::milliseconds(1500));

void FireDebounced() {
    auto now = std::chrono::steady_clock::now();
    if (g_debounce.OnChangeShouldActNow(now)) {
        if (g_onChange) g_onChange();
    }
}

void WatchLoop() {
    struct pollfd fds[2];
    fds[0].fd = g_netlinkFd;
    fds[0].events = POLLIN;
    fds[1].fd = g_wakeupReadFd;
    fds[1].events = POLLIN;
    while (g_running.load()) {
        int result = poll(fds, 2, -1);
        if (result < 0) continue;
        if (fds[1].revents & POLLIN) break;
        if (!(fds[0].revents & POLLIN)) continue;
        char buffer[4096];
        ssize_t bytesRead = recv(g_netlinkFd, buffer, sizeof(buffer), 0);
        if (bytesRead > 0) {
            FireDebounced();
        }
    }
}
#endif
}

bool StartNetworkChangeWatch(std::function<void()> onChange) {
#ifdef __linux__
    g_onChange = std::move(onChange);
    g_netlinkFd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (g_netlinkFd < 0) return false;

    struct sockaddr_nl addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR;
    if (bind(g_netlinkFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(g_netlinkFd);
        g_netlinkFd = -1;
        return false;
    }

    int wakeupFds[2] = { -1, -1 };
    if (pipe(wakeupFds) == 0) {
        g_wakeupReadFd = wakeupFds[0];
        g_wakeupWriteFd = wakeupFds[1];
        fcntl(g_wakeupReadFd, F_SETFL, fcntl(g_wakeupReadFd, F_GETFL) | O_NONBLOCK);
    }

    g_running.store(true);
    g_thread = std::thread(WatchLoop);
    return true;
#else
    (void)onChange;
    return false;
#endif
}

void StopNetworkChangeWatch() {
#ifdef __linux__
    g_running.store(false);
    if (g_wakeupWriteFd >= 0) {
        char zeroByte = 1;
        ssize_t written = write(g_wakeupWriteFd, &zeroByte, 1);
        (void)written;
    }
    if (g_netlinkFd >= 0) { close(g_netlinkFd); g_netlinkFd = -1; }
    if (g_thread.joinable()) g_thread.join();
    if (g_wakeupReadFd >= 0) { close(g_wakeupReadFd); g_wakeupReadFd = -1; }
    if (g_wakeupWriteFd >= 0) { close(g_wakeupWriteFd); g_wakeupWriteFd = -1; }
    g_onChange = nullptr;
#endif
}
