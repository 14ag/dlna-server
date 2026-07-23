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

int         g_lockFd       = -1;
int         g_listenFd     = -1;
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

    g_running = true;
    g_listenerThread = std::thread([]() {
        while (g_running.load()) {
            struct sockaddr_un clientAddr;
            socklen_t clientLen = sizeof(clientAddr);
            const int clientFd = ::accept(
                g_listenFd,
                reinterpret_cast<struct sockaddr*>(&clientAddr),
                &clientLen);

            if (clientFd < 0) {
                if (errno == EINTR) continue;
                break; // listen socket closed or fatal error
            }

            char buf[256] = {};
            const ssize_t n = ::read(clientFd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                std::string cmd(buf);
                // strip trailing newline / carriage-return
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
    // 1. Stop listener.
    g_running = false;
    if (g_listenFd >= 0) {
        ::close(g_listenFd);
        g_listenFd = -1;
    }
    if (g_listenerThread.joinable()) {
        g_listenerThread.join();
    }
    ::unlink(GetSocketPath().c_str());

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
