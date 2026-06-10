#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <vector>
#include <thread>
#include "netutils.h"

struct ConfigSnapshot;

class Server {
public:
    static Server& Get();

    bool Start();
    void Stop();
    bool Rescan();
    
    bool IsRunning() const { return m_running; }
    std::wstring GetEndpoint() const { return m_endpoint; }
    const std::vector<NetworkEndpoint>& GetEndpoints() const { return m_endpoints; }

private:
    Server();
    ~Server();
    void RefreshEndpoints(const ConfigSnapshot& cfg);
    void StartBackgroundScan();
    void JoinBackgroundScan();

    bool m_running;
    std::wstring m_endpoint;
    std::vector<NetworkEndpoint> m_endpoints;
    std::thread m_scanThread;
};

#define DLNAServer Server::Get()

#endif // SERVER_H
