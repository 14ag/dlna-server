#ifdef DLNA_POSIX

#include "posix_single_instance.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <poll.h>

#include <unistd.h>

namespace {

// ---- helpers ----

std::string GetRuntimeDir() {
    if (const char* dir = std::getenv("XDG_RUNTIME_DIR"); dir && *dir) {
        return dir;
    }
    // No XDG_RUNTIME_DIR → fallback to /tmp/dlna-server-<uid>.
    uid_t uid = getuid();
    return std::string("/tmp/dlna-server-") + std::to_string(uid);
}

std::string GetLockPath() {
    return GetRuntimeDir() + "/dlna-server.lock";
}

std::string GetSocketPath() {
    return GetRuntimeDir() + "/dlna-server.sock";
}

void EnsureDirExists(const std::string& path) {
    mkdir(path.c_str(), 0700);
}

// ---- mutable state (accessed only from TryAcquireLock / ReleaseLock / the
//      listener thread or the one thread that calls StartListening before
//      the listener thread has started, so no concurrent access exists.
//      g_running is std::atomic<> for the listener thread's spin check.) ----

int         g_lockFd        = -1;
int         g_listenFd      = -1;
int         g_wakeupReadFd  = -1;
int         g_wakeupWriteFd = -1;
std::thread g_listenerThread;
std::atomic<bool> g_running(false);
void (*g_callback)(const std::string&) = nullptr;

} // anonymous namespace

namespace SingleInstance {

// ---- public API ----

bool TryAcquireLock() {
    const std::string dir = GetRuntimeDir();
    EnsureDirExists(dir);

    const std::string lockPath = GetLockPath();
    g_lockFd = ::open(lockPath.c_str(), O_CREAT | O_RDWR, 0600);
    if (g_lockFd < 0) {
        // Cannot even open the lock file; let the caller proceed.
        return true;
    }

    if (::flock(g_lockFd, LOCK_EX | LOCK_NB) == 0) {
        return true; // lock acquired
    }

    // Another instance holds the lock.
    ::close(g_lockFd);
    g_lockFd = -1;
    return false;
}

bool SendShow() {
    const std::string sockPath = GetSocketPath();
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

    if (::connect(fd, reinterpret_cast<const struct sockaddr*>(&addr),
                  sizeof(addr)) < 0) {
        ::close(fd);
        return false;
    }

    static const char kShow[] = "show\n";
    ::write(fd, kShow, sizeof(kShow) - 1);
    ::close(fd);
    return true;
}

bool SendKill() {
    const std::string sockPath = GetSocketPath();
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

    if (::connect(fd, reinterpret_cast<const struct sockaddr*>(&addr),
                  sizeof(addr)) < 0) {
        ::close(fd);
        return false;
    }

    static const char kKill[] = "kill\n";
    ::write(fd, kKill, sizeof(kKill) - 1);
    ::close(fd);
    return true;
}

void StartListening(void (*onCommand)(const std::string&)) {
    if (g_running.load()) return;
    g_callback = onCommand;

    const std::string sockPath = GetSocketPath();
    ::unlink(sockPath.c_str()); // remove any stale socket file

    g_listenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_listenFd < 0) return;

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

    if (::bind(g_listenFd, reinterpret_cast<const struct sockaddr*>(&addr),
               sizeof(addr)) < 0) {
        ::close(g_listenFd);
        g_listenFd = -1;
        return;
    }

    ::chmod(sockPath.c_str(), 0600);
    ::listen(g_listenFd, 5);

    int wakeupFds[2] = {-1, -1};
    if (::pipe(wakeupFds) == 0) {
        g_wakeupReadFd = wakeupFds[0];
        g_wakeupWriteFd = wakeupFds[1];
        ::fcntl(g_wakeupReadFd, F_SETFL, ::fcntl(g_wakeupReadFd, F_GETFL) | O_NONBLOCK);
    }

    g_running = true;
    g_listenerThread = std::thread([]() {
        while (g_running.load()) {
            struct pollfd fds[2];
            fds[0].fd = g_listenFd;
            fds[0].events = POLLIN;
            fds[0].revents = 0;
            fds[1].fd = g_wakeupReadFd;
            fds[1].events = POLLIN;
            fds[1].revents = 0;
            const nfds_t nfds = g_wakeupReadFd >= 0 ? 2 : 1;

            const int pollResult = ::poll(fds, nfds, -1);
            if (pollResult < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (nfds == 2 && (fds[1].revents & (POLLIN | POLLHUP))) {
                break;
            }
            if (!(fds[0].revents & POLLIN)) continue;

            struct sockaddr_un clientAddr;
            socklen_t clientLen = sizeof(clientAddr);
            const int clientFd = ::accept(
                g_listenFd,
                reinterpret_cast<struct sockaddr*>(&clientAddr),
                &clientLen);

            if (clientFd < 0) {
                if (errno == EINTR) continue;
                break;
            }

            char buf[256] = {};
            const ssize_t n = ::read(clientFd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                std::string cmd(buf);
                while (!cmd.empty() &&
                       (cmd.back() == '\n' || cmd.back() == '\r')) {
                    cmd.pop_back();
                }
                if (g_callback) g_callback(cmd);
            }
            ::close(clientFd);
        }
    });
}

void ReleaseLock() {
    // 1. Stop listener. Wake poll() via the self-pipe *before* touching
    //    g_listenFd -- do not depend on close() alone to unblock a
    //    concurrently-blocked accept() on the same fd; that race is
    //    documented (the fd number can be reused by a descriptor opened
    //    on another thread between close() and the blocked call
    //    returning). Mirrors posix_httpserver.cpp::HttpServer::Stop().
    g_running = false;
    if (g_wakeupWriteFd >= 0) {
        char byte = 0;
        const ssize_t written = ::write(g_wakeupWriteFd, &byte, 1);
        (void)written;
    }
    if (g_listenFd >= 0) {
        ::close(g_listenFd);
        g_listenFd = -1;
    }
    if (g_listenerThread.joinable()) {
        g_listenerThread.join();
    }
    ::unlink(GetSocketPath().c_str());
    if (g_wakeupReadFd >= 0) {
        ::close(g_wakeupReadFd);
        g_wakeupReadFd = -1;
    }
    if (g_wakeupWriteFd >= 0) {
        ::close(g_wakeupWriteFd);
        g_wakeupWriteFd = -1;
    }

    // 2. Release file lock.
    if (g_lockFd >= 0) {
        ::flock(g_lockFd, LOCK_UN);
        ::close(g_lockFd);
        g_lockFd = -1;
        ::unlink(GetLockPath().c_str());
    }
}

} // namespace SingleInstance

#endif // DLNA_POSIX
