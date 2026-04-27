#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <vector>
#include "netutils.h"

class Server {
public:
    static Server& Get();

    bool Start();
    void Stop();
    
    bool IsRunning() const { return m_running; }
    std::wstring GetEndpoint() const { return m_endpoint; }
    const std::vector<NetworkEndpoint>& GetEndpoints() const { return m_endpoints; }

private:
    Server();
    void RefreshEndpoints();

    bool m_running;
    std::wstring m_endpoint;
    std::vector<NetworkEndpoint> m_endpoints;
};

#define DLNAServer Server::Get()

#endif // SERVER_H
