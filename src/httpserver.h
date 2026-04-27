#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <string>
#include <winsock2.h>

class HttpServer {
public:
    static HttpServer& Get();

    bool Start(int port);
    void Stop();
    
    void HandleClient(SOCKET clientSocket, const std::string& clientIP);

private:
    HttpServer();
    static DWORD WINAPI AcceptThreadWorker(LPVOID lpParam);
    static void CALLBACK WorkerCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work);

    bool m_running;
    HANDLE m_hAcceptThread;
    SOCKET m_listenSocketV4;
    SOCKET m_listenSocketV6;

    PTP_POOL m_threadPool;
    PTP_CLEANUP_GROUP m_cleanupGroup;
    TP_CALLBACK_ENVIRON m_cbe;
};

#endif // HTTPSERVER_H
