#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <string>
#include <atomic>
#include "bounded_thread_pool.h"
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#endif

class HttpServer {
public:
    static HttpServer& Get();

    bool Start(int port);
    void Stop();

#ifdef _WIN32
    void HandleClient(SOCKET clientSocket, const std::string& clientIP);
#else
    void HandleClient(int clientSocket, const std::string& clientIP);
#endif

private:
    HttpServer();
#ifdef _WIN32
    static DWORD WINAPI AcceptThreadWorker(LPVOID lpParam);
    static void CALLBACK WorkerCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work);

    std::atomic<bool> m_running;
    HANDLE m_hAcceptThread;
    SOCKET m_listenSocketV4;
    SOCKET m_listenSocketV6;

    PTP_POOL m_threadPool;
    PTP_CLEANUP_GROUP m_cleanupGroup;
    TP_CALLBACK_ENVIRON m_cbe;
    // bounds accepted-but-not-yet-finished clients the same way
    // kMaxClientThreads already bounds them on the posix side see
    // posix_httpserver cpp AcceptLoop for the reference this mirrors
    std::atomic<size_t> m_activeClientCount{0};
#else
    void AcceptLoop(int listenSocket);

    std::atomic<bool> m_running;
    int m_listenSocketV4;
    int m_listenSocketV6;
    std::vector<std::thread> m_threads;
    // Bounded, pre-spawned worker pool for connection handling, replacing
    // a std::thread spawned per accepted connection. Sized to
    // kMaxClientThreads for both worker count and queue depth, mirroring
    // the pre-spawn-and-queue model CivetWeb documents for its own
    // num_threads/connection_queue options, and matching the persistent
    // Windows Thread Pool this project's own httpserver.cpp (Win32) already
    // uses. See Task 8 of
    // dlna-server-qa-audit-and-posix-gui-lifecycle-workflow-05-08-26.md.
    std::unique_ptr<BoundedThreadPool> m_clientPool;
    int m_wakeupReadFd;
    int m_wakeupWriteFd;
#endif
};

#endif // HTTPSERVER_H
