#include "dirwatch.h"
#include "fs_change_debounce.h"
#include "log.h"

#include <windows.h>
#include <vector>
#include <thread>
#include <atomic>

namespace {
struct WatchedFolder {
    HANDLE dirHandle = INVALID_HANDLE_VALUE;
    OVERLAPPED overlapped = {};
    std::vector<BYTE> buffer;
};

std::vector<WatchedFolder> g_folders;
std::atomic<bool> g_running(false);
std::thread g_thread;
std::function<void()> g_onChange;
FsChangeDebouncer g_debounce(std::chrono::milliseconds(2000));

void CALLBACK CompletionRoutine(DWORD, DWORD bytesTransferred, LPOVERLAPPED overlapped) {
    if (bytesTransferred == 0) {
        LogPrint(L"Directory watch buffer overflow; falling back to safety net poll for this window");
    }
    auto now = std::chrono::steady_clock::now();
    if (g_debounce.OnChangeShouldActNow(now)) {
        if (g_onChange) g_onChange();
    }
    for (auto& folder : g_folders) {
        if (&folder.overlapped == overlapped) {
            ReadDirectoryChangesW(folder.dirHandle, folder.buffer.data(),
                static_cast<DWORD>(folder.buffer.size()), TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
                NULL, &folder.overlapped, CompletionRoutine);
            break;
        }
    }
}

void WatchLoopThread() {
    while (g_running.load()) {
        SleepEx(1000, TRUE);
    }
}
}

bool StartDirectoryWatch(const std::vector<std::wstring>& localFolders, std::function<void()> onChange) {
    g_onChange = std::move(onChange);
    bool anyOk = false;
    for (const auto& path : localFolders) {
        WatchedFolder folder;
        folder.dirHandle = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
        if (folder.dirHandle == INVALID_HANDLE_VALUE) continue;
        folder.buffer.resize(65536);
        g_folders.push_back(std::move(folder));
        anyOk = true;
    }
    if (!anyOk) return false;

    g_running.store(true);
    for (auto& folder : g_folders) {
        ReadDirectoryChangesW(folder.dirHandle, folder.buffer.data(),
            static_cast<DWORD>(folder.buffer.size()), TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
            NULL, &folder.overlapped, CompletionRoutine);
    }
    g_thread = std::thread(WatchLoopThread);
    return true;
}

void StopDirectoryWatch() {
    g_running.store(false);
    for (auto& folder : g_folders) {
        if (folder.dirHandle != INVALID_HANDLE_VALUE) {
            CancelIoEx(folder.dirHandle, &folder.overlapped);
            CloseHandle(folder.dirHandle);
        }
    }
    g_folders.clear();
    if (g_thread.joinable()) g_thread.join();
    g_onChange = nullptr;
}
