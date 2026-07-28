#include "dirwatch.h"
#include "fs_change_debounce.h"
#include "log.h"
#include "netutils.h"

#ifdef __linux__
#include <sys/inotify.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <filesystem>
#include <unordered_map>
#include <thread>
#include <atomic>

namespace {
namespace fs = std::filesystem;

int g_inotifyFd = -1;
int g_wakeupReadFd = -1;
int g_wakeupWriteFd = -1;
std::thread g_thread;
std::atomic<bool> g_running(false);
std::function<void()> g_onChange;
FsChangeDebouncer g_debounce(std::chrono::milliseconds(2000));
std::unordered_map<int, std::string> g_watchToPath;

constexpr uint32_t kWatchMask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE;

void AddWatchRecursive(const std::string& rootPath) {
    std::error_code ec;
    int watchDescriptor = inotify_add_watch(g_inotifyFd, rootPath.c_str(), kWatchMask);
    if (watchDescriptor < 0) {
        LogPrint(L"inotify watch registration failed for %hs; falling back to poll for this path. Check max_user_watches.", rootPath.c_str());
        return;
    }
    g_watchToPath[watchDescriptor] = rootPath;
    fs::directory_iterator it(rootPath, fs::directory_options::skip_permission_denied, ec);
    fs::directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (it->is_directory(entryEc) && !entryEc) {
            AddWatchRecursive(it->path().string());
        }
    }
}

void WatchLoop() {
    char buffer[sizeof(struct inotify_event) + 256 + 1];
    while (g_running.load()) {
        struct pollfd fds[2];
        fds[0].fd = g_inotifyFd;
        fds[0].events = POLLIN;
        fds[1].fd = g_wakeupReadFd;
        fds[1].events = POLLIN;
        int result = poll(fds, 2, -1);
        if (result < 0) continue;
        if (fds[1].revents & POLLIN) break;
        if (!(fds[0].revents & POLLIN)) continue;

        ssize_t bytesRead = read(g_inotifyFd, buffer, sizeof(buffer));
        if (bytesRead <= 0) continue;

        size_t offset = 0;
        bool sawEvent = false;
        while (offset < static_cast<size_t>(bytesRead)) {
            auto* event = reinterpret_cast<struct inotify_event*>(buffer + offset);
            sawEvent = true;
            if ((event->mask & IN_ISDIR) && (event->mask & IN_CREATE)) {
                auto found = g_watchToPath.find(event->wd);
                if (found != g_watchToPath.end() && event->len > 0) {
                    AddWatchRecursive(found->second + "/" + event->name);
                }
            }
            offset += sizeof(struct inotify_event) + event->len;
        }
        if (sawEvent) {
            auto now = std::chrono::steady_clock::now();
            if (g_debounce.OnChangeShouldActNow(now) && g_onChange) {
                g_onChange();
            }
        }
    }
}
}

bool StartDirectoryWatch(const std::vector<std::wstring>& localFolders, std::function<void()> onChange) {
    g_onChange = std::move(onChange);
    g_inotifyFd = inotify_init1(IN_NONBLOCK);
    if (g_inotifyFd < 0) return false;

    for (const auto& wideFolder : localFolders) {
        AddWatchRecursive(WideToUtf8(wideFolder));
    }
    if (g_watchToPath.empty()) {
        close(g_inotifyFd);
        g_inotifyFd = -1;
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
}

void StopDirectoryWatch() {
    g_running.store(false);
    if (g_wakeupWriteFd >= 0) {
        char oneByte = 1;
        ssize_t written = write(g_wakeupWriteFd, &oneByte, 1);
        (void)written;
    }
    if (g_inotifyFd >= 0) { close(g_inotifyFd); g_inotifyFd = -1; }
    if (g_thread.joinable()) g_thread.join();
    if (g_wakeupReadFd >= 0) { close(g_wakeupReadFd); g_wakeupReadFd = -1; }
    if (g_wakeupWriteFd >= 0) { close(g_wakeupWriteFd); g_wakeupWriteFd = -1; }
    g_watchToPath.clear();
    g_onChange = nullptr;
}

#else

bool StartDirectoryWatch(const std::vector<std::wstring>&, std::function<void()>) {
    return false;
}

void StopDirectoryWatch() {
}

#endif
