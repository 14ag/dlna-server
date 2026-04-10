#ifndef SERVER_H
#define SERVER_H

#include <string>

class Server {
public:
    static Server& Get();

    bool Start();
    void Stop();
    
    bool IsRunning() const { return m_running; }
    std::wstring GetEndpoint() const { return m_endpoint; }

private:
    Server();
    std::string GetLocalIPv4();

    bool m_running;
    std::wstring m_endpoint;
};

#define DLNAServer Server::Get()

#endif // SERVER_H
