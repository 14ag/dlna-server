#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <string>
#include <atomic>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
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
#else
    void AcceptLoop(int listenSocket);

    std::atomic<bool> m_running;
    int m_listenSocketV4;
    int m_listenSocketV6;
    std::vector<std::thread> m_threads;
    std::mutex m_clientMutex;
    std::vector<std::thread> m_clientThreads;
#endif
};

#endif // HTTPSERVER_H
